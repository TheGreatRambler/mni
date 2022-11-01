import 'dart:io';

import 'package:flutter/material.dart';
import 'package:mnicodes/data/state.dart';
import 'package:provider/provider.dart';

class HomePage extends StatefulWidget {
  @override
  _HomePageState createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> with WidgetsBindingObserver {
  final List<AppLifecycleState> stateHistory = <AppLifecycleState>[];
  bool isLoading = true;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);

    prepareState();
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    super.dispose();
  }

  Future<void> prepareState() async {
    CurrentState currentState = Provider.of<CurrentState>(context, listen: false);
    WidgetsBinding.instance.addPostFrameCallback((timeStamp) async {
      await currentState.start();

      PluginAccess pluginAccess = Provider.of<PluginAccess>(context, listen: false);
      await pluginAccess.loadCamera();

      setState(() {
        isLoading = false;
      });
    });

    //WidgetsBinding.instance.addPersistentFrameCallback((timeStamp) async {
    //  // This doesn't actually tick every frame, go figures
    //  print("Ok ticking");
    //  await currentState.renderNextFrame();
    //});
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) async {
    super.didChangeAppLifecycleState(state);
    stateHistory.add(state);
    if (state == AppLifecycleState.inactive) {
      // Remove camera
      PluginAccess pluginAccess = Provider.of<PluginAccess>(context, listen: false);
      await pluginAccess.disposeCamera();
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
        appBar: AppBar(
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
        body: isLoading // Texture 0 is actuallty the camera
            ? const Center(child: CircularProgressIndicator())
            : Consumer<CurrentState>(
                builder: (context, currentState, child) {
                  return Center(
                    child: Container(
                      width: 512,
                      height: 512,
                      child: Texture(textureId: currentState.texture),
                    ),
                  );
                },
              ));
  }
}
