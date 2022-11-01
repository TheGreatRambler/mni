import 'dart:typed_data';

import 'package:flutter/services.dart';

// https://www.evertop.pl/en/mediastore-in-flutter/
// Allows for downloads
class MediaStore {
  static const _channel = MethodChannel("flutter_media_store");

  Future<void> addItem(String file, String name, String mime) async {
    await _channel.invokeMethod("addItem", {"path": file, "name": name, "mime": mime});
  }

  Future<int> createTexture(Uint8List buffer) async {
    return await _channel.invokeMethod("createTexture", {"buffer": buffer});
  }
}
