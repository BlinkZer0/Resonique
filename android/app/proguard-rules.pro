# The page calls these through window.AndroidMidi and window.AndroidFiles
-keepclassmembers class * {
    @android.webkit.JavascriptInterface <methods>;
}
