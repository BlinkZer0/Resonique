package com.blinkzer0.resonique;

import android.content.Context;
import android.content.pm.PackageManager;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiManager;
import android.media.midi.MidiOutputPort;
import android.media.midi.MidiReceiver;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.webkit.JavascriptInterface;
import android.webkit.WebView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Native side of the Web MIDI shim (assets/android-midi.js).
 *
 * Opens every MIDI device that sends data (USB class-compliant keyboards over
 * OTG, plus Bluetooth/virtual devices already connected to Android), frames
 * the byte stream into channel messages and forwards them to the page in
 * batches: window.__androidMidi.receive([[portId, b0, b1, b2], ...]).
 * Device add/remove is reported with window.__androidMidi.devices([...]).
 *
 * All device bookkeeping happens on the main thread; MIDI data arrives on a
 * MidiManager binder thread and is queued under {@link #pendingLock}.
 */
final class MidiBridge {
    private final WebView web;
    private final MidiManager manager;
    private final Handler main = new Handler(Looper.getMainLooper());

    /** Open devices by MidiDeviceInfo id. Main thread only. */
    private final Map<Integer, OpenDevice> open = new HashMap<>();
    private boolean started;
    private boolean closed;

    private final Object pendingLock = new Object();
    private StringBuilder pending = new StringBuilder();
    private boolean flushScheduled;

    private final MidiManager.DeviceCallback callback = new MidiManager.DeviceCallback() {
        @Override public void onDeviceAdded(MidiDeviceInfo info) { openDevice(info); }
        @Override public void onDeviceRemoved(MidiDeviceInfo info) { closeDevice(info.getId()); pushDevices(); }
    };

    MidiBridge(Context context, WebView web) {
        this.web = web;
        boolean hasMidi = context.getPackageManager().hasSystemFeature(PackageManager.FEATURE_MIDI);
        this.manager = hasMidi ? (MidiManager) context.getSystemService(Context.MIDI_SERVICE) : null;
    }

    // ------------------------------------------------------------ JS API ---

    @JavascriptInterface
    public boolean isAvailable() { return manager != null; }

    /** Starts listening (idempotent) and returns the current input list as JSON. */
    @JavascriptInterface
    public String start() {
        if (manager == null) return "[]";
        main.post(this::begin);
        return inputsJson(currentDevices()).toString();
    }

    // ------------------------------------------------------------ devices ---

    @SuppressWarnings("deprecation")   // getDevices/registerDeviceCallback: fine on all API levels
    private MidiDeviceInfo[] currentDevices() {
        return manager.getDevices();
    }

    @SuppressWarnings("deprecation")
    private void begin() {
        if (started || closed) return;
        started = true;
        manager.registerDeviceCallback(callback, main);
        for (MidiDeviceInfo info : currentDevices()) openDevice(info);
    }

    private void openDevice(MidiDeviceInfo info) {
        if (closed || info.getOutputPortCount() == 0 || open.containsKey(info.getId())) return;
        final OpenDevice od = new OpenDevice(info);
        open.put(info.getId(), od);
        manager.openDevice(info, device -> {
            if (device == null) { open.remove(info.getId()); pushDevices(); return; }
            if (closed || open.get(info.getId()) != od) { closeQuietly(device); return; }
            od.device = device;
            for (int p = 0; p < info.getOutputPortCount(); p++) {
                MidiOutputPort port = device.openOutputPort(p);   // null if another app holds it exclusively
                if (port == null) continue;
                port.connect(new Framer(portId(info, p)));
                od.ports.add(port);
            }
            pushDevices();
        }, main);
    }

    private void closeDevice(int id) {
        OpenDevice od = open.remove(id);
        if (od == null) return;
        for (MidiOutputPort p : od.ports) closeQuietly(p);
        if (od.device != null) closeQuietly(od.device);
    }

    void close() {
        main.post(() -> {
            if (closed) return;
            closed = true;
            if (manager != null && started) manager.unregisterDeviceCallback(callback);
            for (Integer id : new ArrayList<>(open.keySet())) closeDevice(id);
        });
    }

    private void pushDevices() {
        if (closed) return;
        JSONArray list = inputsJson(currentDevices());
        web.evaluateJavascript("window.__androidMidi&&window.__androidMidi.devices(" + list + ")", null);
    }

    private static String portId(MidiDeviceInfo info, int port) { return info.getId() + ":" + port; }

    private static JSONArray inputsJson(MidiDeviceInfo[] devices) {
        JSONArray arr = new JSONArray();
        for (MidiDeviceInfo info : devices) {
            int n = info.getOutputPortCount();
            Bundle props = info.getProperties();
            String name = props.getString(MidiDeviceInfo.PROPERTY_NAME);
            if (name == null || name.isEmpty()) name = props.getString(MidiDeviceInfo.PROPERTY_PRODUCT);
            if (name == null || name.isEmpty()) name = "MIDI device " + info.getId();
            String maker = props.getString(MidiDeviceInfo.PROPERTY_MANUFACTURER);
            for (MidiDeviceInfo.PortInfo pi : info.getPorts()) {
                if (pi.getType() != MidiDeviceInfo.PortInfo.TYPE_OUTPUT) continue;
                String label = name;
                if (n > 1) {
                    String pn = pi.getName();
                    label += " " + (pn != null && !pn.isEmpty() ? pn : String.valueOf(pi.getPortNumber() + 1));
                }
                try {
                    arr.put(new JSONObject()
                            .put("id", portId(info, pi.getPortNumber()))
                            .put("name", label)
                            .put("manufacturer", maker == null ? "" : maker));
                } catch (org.json.JSONException ignored) { }
            }
        }
        return arr;
    }

    private static void closeQuietly(java.io.Closeable c) {
        try { c.close(); } catch (IOException ignored) { }
    }

    private static final class OpenDevice {
        final MidiDeviceInfo info;
        MidiDevice device;
        final List<MidiOutputPort> ports = new ArrayList<>();
        OpenDevice(MidiDeviceInfo info) { this.info = info; }
    }

    // --------------------------------------------------------- data path ---

    private void emit(String id, int status, int d1, int d2, int len) {
        synchronized (pendingLock) {
            pending.append(pending.length() == 0 ? '[' : ',')
                   .append("[\"").append(id).append("\",").append(status).append(',').append(d1);
            if (len == 2) pending.append(',').append(d2);
            pending.append(']');
            if (flushScheduled) return;
            flushScheduled = true;
        }
        main.post(this::flush);
    }

    private void flush() {
        String batch;
        synchronized (pendingLock) {
            flushScheduled = false;
            if (pending.length() == 0) return;
            batch = pending.append(']').toString();
            pending = new StringBuilder();
        }
        if (closed) return;
        web.evaluateJavascript("window.__androidMidi&&window.__androidMidi.receive(" + batch + ")", null);
    }

    /**
     * Splits a raw MIDI byte stream into channel voice messages. Handles
     * running status and interleaved real-time bytes; drops SysEx and other
     * system messages (the piano ignores them).
     */
    private final class Framer extends MidiReceiver {
        private final String id;
        private int status, need, have, d1;

        Framer(String id) { this.id = id; }

        @Override
        public void onSend(byte[] msg, int offset, int count, long timestamp) {
            for (int i = offset, end = offset + count; i < end; i++) {
                int b = msg[i] & 0xFF;
                if (b >= 0xF8) continue;                      // real-time: clock, active sensing…
                if (b >= 0xF0) { status = 0; continue; }      // SysEx / system common: ignore
                if (b >= 0x80) {
                    status = b;
                    int cmd = b & 0xF0;
                    need = (cmd == 0xC0 || cmd == 0xD0) ? 1 : 2;
                    have = 0;
                    continue;
                }
                if (status == 0) continue;                    // SysEx payload or stray data
                if (have == 0) {
                    d1 = b;
                    have = 1;
                    if (need == 1) { emit(id, status, d1, 0, 1); have = 0; }
                } else {
                    emit(id, status, d1, b, 2);
                    have = 0;                                 // keep status: running status
                }
            }
        }
    }
}
