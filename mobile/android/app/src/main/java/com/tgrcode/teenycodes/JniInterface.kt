package com.tgrcode.teenycodes

class JniInterface {
	companion object {
		init {
			System.loadLibrary("tinycode_android_native")
		}
	}

	external fun registerCode(qr: ByteArray): Void
	external fun getCodeName(): String
}