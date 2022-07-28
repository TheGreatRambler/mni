package com.tgrcode.teenycodes

class JniInterface {
    companion object {
        init {
            System.loadLibrary("tinycode_android_native")
        }
    }

    external fun getAwesomeMessage(): String
}