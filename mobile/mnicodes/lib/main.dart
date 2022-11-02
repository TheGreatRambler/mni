import 'package:flutter/material.dart';
import 'package:mnicodes/views/home.dart';
import 'package:mnicodes/data/state.dart';
import 'package:provider/provider.dart';
import 'package:flutter/services.dart';

void main() async {
  // Enforce portrait only
  WidgetsFlutterBinding.ensureInitialized();
  await SystemChrome.setPreferredOrientations(
    [DeviceOrientation.portraitUp],
  );

  runApp(
    MultiProvider(
      providers: [
        ChangeNotifierProvider(create: (context) => CurrentState()),
        Provider(create: (context) => PluginAccess()),
      ],
      child: App(),
    ),
  );
}

class App extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: "mni.codes",
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        primarySwatch: Colors.blue,
        primaryColor: Colors.white,
        textTheme: const TextTheme(
          bodyText2: TextStyle(color: Colors.white),
        ),
      ),
      darkTheme: ThemeData(
        brightness: Brightness.dark,
        /* dark theme settings */
      ),
      themeMode: ThemeMode.system, //ThemeMode.dark,
      home: HomePage(),
    );
  }
}
