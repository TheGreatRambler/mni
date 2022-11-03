import 'dart:ffi';
import 'dart:typed_data';

import 'package:flutter/services.dart';

// https://www.evertop.pl/en/mediastore-in-flutter/
// Allows for downloads
class NativeBridge {
  static const _channel = MethodChannel("native_bridge");

  Future<void> addDownload(String file, String name, String mime) async {
    await _channel.invokeMethod("addDownload", {"path": file, "name": name, "mime": mime});
  }

  Future<int> createTexture(Uint8List buffer) async {
    return await _channel.invokeMethod("createTexture", {"buffer": buffer});
  }

  Future<void> startPress(double x, double y) async {
    return await _channel.invokeMethod("startPress", {"x": x, "y": y});
  }

  Future<void> holdPress(double x, double y) async {
    return await _channel.invokeMethod("holdPress", {"x": x, "y": y});
  }

  Future<void> endPress() async {
    return await _channel.invokeMethod("endPress");
  }
}
