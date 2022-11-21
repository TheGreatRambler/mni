import 'dart:typed_data';

import 'package:camera/camera.dart';
import 'package:flutter/material.dart';
import 'package:image/image.dart' as img;
import 'package:mnicodes/data/state.dart';
import 'package:provider/provider.dart';
import 'package:google_mlkit_barcode_scanning/google_mlkit_barcode_scanning.dart';
import 'package:mnicodes/views/mni_view.dart';

class ScanForQR extends StatefulWidget {
  ScanForQR({
    Key? key,
  }) : super(key: key);

  @override
  _ScanForQRState createState() => _ScanForQRState();
}

class _ScanForQRState extends State<ScanForQR> {
  bool isLoadingCamera = true;

  _ScanForQRState();

  bool openCamera(BuildContext context) {
    PluginAccess pluginAccess = Provider.of<PluginAccess>(context);

    if (!isLoadingCamera) {
      return true;
    }

    pluginAccess.loadCamera().then((value) {
      pluginAccess.rearCameraWait?.then((value) {
        setState(() {
          isLoadingCamera = false;
        });
      });
    });

    return false;
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        automaticallyImplyLeading: false,
        centerTitle: true,
        title: Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: const [
            Icon(Icons.restaurant_menu),
            SizedBox(width: 10),
            Text("Take Picture"),
          ],
        ),
      ),
      body: Consumer<PluginAccess>(builder: (context, pluginAccess, child) {
        return openCamera(context)
            ? CameraPreview(pluginAccess.rearCamera!)
            : const Center(child: CircularProgressIndicator());
      }),
      floatingActionButton: Consumer<PluginAccess>(builder: (context, pluginAccess, child) {
        return Consumer<CurrentState>(builder: (context, currentState, child) {
          return FloatingActionButton(
            onPressed: () async {
              try {
                await pluginAccess.loadCamera();
                await pluginAccess.rearCameraWait;
                pluginAccess.rearCamera?.setFlashMode(FlashMode.off);
                pluginAccess.rearCamera?.takePicture().then((image) async {
                  var barcodeScanner = BarcodeScanner(formats: [BarcodeFormat.qrCode]);
                  final List<Barcode> barcodes = await barcodeScanner.processImage(InputImage.fromFilePath(image.path));
                  if (barcodes.isNotEmpty) {
                    print("Found barcode ${barcodes[0].rawBytes!.length}");
                    currentState.loadFromQR(barcodes[0].rawBytes!);
                  }

                  Navigator.of(context).push(PageRouteBuilder(
                    pageBuilder: (context, animation, secondaryAnimation) => MniView(),
                    transitionsBuilder: (context, animation, secondaryAnimation, child) {
                      const begin = Offset(1.0, 0.0);
                      const end = Offset.zero;
                      const curve = Curves.ease;

                      var tween = Tween(begin: begin, end: end).chain(CurveTween(curve: curve));

                      return SlideTransition(
                        position: animation.drive(tween),
                        child: child,
                      );
                    },
                  ));
                });
              } catch (e) {
                // If an error occurs, log the error to the console.
                print(e);
              }
            },
            tooltip: "Take picture",
            child: const Icon(Icons.camera_alt),
          );
        });
      }),
    );
  }
}
