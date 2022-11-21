import 'package:flutter/material.dart';
import 'package:mnicodes/data/state.dart';
import 'package:mnicodes/data/nativebridge.dart';
import 'package:provider/provider.dart';

class MniView extends StatelessWidget {
  double renderWidth = 512;
  double renderHeight = 512;

  MniView();

  @override
  Widget build(BuildContext context) {
    return Consumer<CurrentState>(
      builder: (context, currentState, child) {
        return WillPopScope(
          child: Scaffold(
            resizeToAvoidBottomInset: false,
            appBar: AppBar(
              automaticallyImplyLeading: false,
              centerTitle: true,
              title: Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: const [
                  Icon(Icons.code),
                  SizedBox(width: 10),
                  Text("mni.codes"),
                ],
              ),
            ),
            body: Padding(
              padding: const EdgeInsets.all(8.0),
              child: LayoutBuilder(builder: (context, constraints) {
                return SizedBox(
                    width: constraints.maxWidth,
                    height: constraints.maxHeight,
                    child: GestureDetector(
                        onPanStart: (e) async {
                          var renderBox = context.findRenderObject() as RenderBox;
                          var scaleX = renderBox.size.width / renderWidth;
                          var scaleY = renderBox.size.height / renderHeight;
                          await NativeBridge().startPress(e.localPosition.dx / scaleX, e.localPosition.dy * scaleY);
                        },
                        onPanUpdate: (e) async {
                          var renderBox = context.findRenderObject() as RenderBox;
                          var scaleX = renderBox.size.width / renderWidth;
                          var scaleY = renderBox.size.height / renderHeight;
                          await NativeBridge().holdPress(e.localPosition.dx / scaleX, e.localPosition.dy * scaleY);
                        },
                        onPanEnd: (e) async {
                          await NativeBridge().endPress();
                        },
                        child: Texture(
                          textureId: currentState.texture,
                          filterQuality: FilterQuality.none,
                        )));
              }),
            ),
          ),
          onWillPop: () async {
            // TODO
            return true;
          },
        );
      },
    );
  }
}
