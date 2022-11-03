import 'dart:ffi';
import 'dart:typed_data';
import 'dart:developer';
import 'dart:io';

import 'package:sqflite/sqflite.dart';
import 'package:image/image.dart' as img;
import 'package:camera/camera.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:mnicodes/data/nativebridge.dart';

class CurrentState extends ChangeNotifier {
  int texture = 0;

  Future<void> start() async {
    var temp = await rootBundle.load("assets/main.owasm");
    texture = await NativeBridge().createTexture(temp.buffer.asUint8List());
  }
}

class PluginAccess {
  CameraController? rearCamera;
  late Future<void>? rearCameraWait;

  Future<void> loadCamera() async {
    if (rearCamera == null) {
      WidgetsFlutterBinding.ensureInitialized();

      final cameras = await availableCameras();
      rearCamera = CameraController(
        cameras.first,
        ResolutionPreset.low,
        enableAudio: false,
      );

      rearCameraWait = rearCamera?.initialize();
    }
  }

  Future<void> disposeCamera() async {
    await rearCamera?.dispose();
    rearCamera = null;
  }
}

/*
class RecipeDatabase {
  Database? db;

  Future open(String path) async {
    //File(join(await getDatabasesPath(), path)).delete();
    db = await openDatabase(join(await getDatabasesPath(), path), version: 1,
        onCreate: (Database db, int version) async {
      await db.execute('''
CREATE TABLE IF NOT EXISTS recipes ( 
    _id integer primary key autoincrement, 
    name text not null,
    expected_servings real not null,
    url integer not null,
    thumbnail blob not null,
    last_synced integer)
''');

      await db.execute('''
CREATE TABLE IF NOT EXISTS recipe_ingredients ( 
    _id integer primary key autoincrement,
    recipe integer not null,
    volume_type text not null,
    volume_quantity real not null,
    store_ingredient integer not null)
''');

      await db.execute('''
CREATE TABLE IF NOT EXISTS store_ingredients ( 
    _id integer primary key autoincrement, 
    name text not null,
    volume_type text not null,
    volume_quantity real not null,
    price integer not null,
    thumbnail blob not null,
    last_synced integer)
''');
    }, onOpen: (Database db) async {});
  }

  Future<StoreIngredient> insertIngredient(StoreIngredient ingredient) async {
    ingredient.id = await db?.insert("store_ingredients", ingredient.toMap());
    return ingredient;
  }

  Future<List<StoreIngredient>> getAllIngredients() async {
    List<Map<String, Object?>>? ingredients = await db?.query("store_ingredients", columns: [
      "_id",
      "name",
      "volume_type",
      "volume_quantity",
      "price",
      "thumbnail",
      "last_synced",
    ]);

    return ingredients == null ? [] : ingredients.map((map) => StoreIngredient.fromMap(map)).toList();
  }

  Future<void> deleteIngredient(StoreIngredient ingredient) async {
    await db?.delete("store_ingredients", where: "_id = ?", whereArgs: [ingredient.id]);
    // TODO check if anything is using this store ingredient
  }

  Future<void> updateIngredient(StoreIngredient ingredient) async {
    var ingredientMap = ingredient.toMap();
    await db?.update("store_ingredients", ingredientMap, where: "_id = ?", whereArgs: [ingredient.id]);
  }

  Future<Recipe> insertRecipe(Recipe recipe) async {
    recipe.id = await db?.insert("recipes", recipe.toMap());

    for (var ingredient in recipe.ingredients) {
      var ingredientMap = ingredient.toMap();
      ingredientMap["recipe"] = recipe.id;
      ingredient.id = await db?.insert("recipe_ingredients", ingredientMap);
    }

    return recipe;
  }

  Future<String?> getRecipeBackup(List<Recipe> recipes, [String? name]) async {
    // TODO this is no longer neccesary
    //var canAccessExternalStorage = await Permission.storage.request().isGranted;
    //if (!canAccessExternalStorage) {
    //  // Uh oh
    //  return "";
    //}

    // Create new database for this backup
    // TODO: Certain charactors in the recipe name WILL NOT SAVE! (question marks are one)
    final DateFormat dateFormatter = DateFormat("yyyy-MM-dd-H-m-s");
    String databaseName = "mealsave-${name ?? recipes[0].name}-${dateFormatter.format(DateTime.now())}.mealsave";

    Directory basePath = await getTemporaryDirectory();
    String databasePath = join(basePath.path, databaseName);

    var backupDb = await openDatabase(databasePath, version: 1, onConfigure: (Database backupDb) async {
      // Disable db-journal
      // It's an extra file that users couldn't use
      await backupDb.rawQuery("PRAGMA journal_mode=MEMORY;");
    }, onCreate: (Database backupDb, int version) async {
      await backupDb.execute('''
CREATE TABLE IF NOT EXISTS recipes ( 
    _id integer primary key autoincrement, 
    name text not null,
    expected_servings real not null,
    url text not null,
    thumbnail blob not null)
''');

      await backupDb.execute('''
CREATE TABLE IF NOT EXISTS recipe_ingredients ( 
    _id integer primary key autoincrement,
    recipe integer not null,
    volume_type text not null,
    volume_quantity real not null,
    store_ingredient integer not null)
''');

      await backupDb.execute('''
CREATE TABLE IF NOT EXISTS store_ingredients ( 
    _id integer primary key autoincrement, 
    name text not null,
    volume_type text not null,
    volume_quantity real not null,
    price integer not null,
    thumbnail blob not null)
''');
    }, onOpen: (Database backupDb) async {
      // Insert into backup
      var batch = backupDb.batch();
      for (var recipe in recipes) {
        var recipeMap = recipe.toMap();
        recipeMap.remove("last_synced");
        batch.insert("recipes", recipeMap);
      }
      await batch.commit(noResult: true);

      List<Map<String, Object?>> recipeIngredients = await db?.query("recipe_ingredients",
              columns: [
                "_id",
                "recipe",
                "volume_type",
                "volume_quantity",
                "store_ingredient",
              ],
              where: "recipe IN (${recipes.map((recipe) => recipe.id).join(',')})") ??
          [];

      var allIncludedStoreIngredients = <int>{};
      for (var recipeIngredient in recipeIngredients) {
        await backupDb.insert("recipe_ingredients", recipeIngredient);

        var id = recipeIngredient["store_ingredient"] as int;
        if (!allIncludedStoreIngredients.contains(id)) {
          List<Map<String, Object?>> storeIngredients = await db?.query("store_ingredients",
                  columns: [
                    "_id",
                    "name",
                    "volume_type",
                    "volume_quantity",
                    "price",
                    "thumbnail",
                  ],
                  where: "_id = ?",
                  whereArgs: [id]) ??
              [];

          if (storeIngredients.isNotEmpty) {
            await backupDb.insert("store_ingredients", storeIngredients[0]);
            allIncludedStoreIngredients.add(id);
          }
        }
      }
    });

    await backupDb.close();

    // Send to downloads folder
    await MediaStore().addItem(databasePath, databaseName, "application/mealsave-backup");

    // Delete the old file
    await File(databasePath).delete();

    return databaseName;
  }

  Future<BackupRecipe> loadBackupRecipe(File path) async {
    if (!await path.exists()) {
      return BackupRecipe(returnedIngredients: [], returnedRecipes: []);
    }

    var backupDb = await openDatabase(path.path, version: 1);

    // Get all store ingredients
    List<Map<String, Object?>> ingredientsMaps = await backupDb.query("store_ingredients", columns: [
      "_id",
      "name",
      "volume_type",
      "volume_quantity",
      "price",
      "thumbnail",
    ]); //.map((map) => map.);
    // https://github.com/tekartik/sqflite/issues/195#issuecomment-484528618
    ingredientsMaps = List<Map<String, dynamic>>.generate(
        ingredientsMaps.length, (index) => Map<String, dynamic>.from(ingredientsMaps[index]),
        growable: true);

    List<StoreIngredient> returnedIngredients = [];
    Map<int, StoreIngredient> ingredientFromID = <int, StoreIngredient>{};
    Map<int, int> backupIDToActualID = <int, int>{};

    for (var ingredient in ingredientsMaps) {
      // Load old ID
      int oldId = ingredient["_id"] as int;

      // Remove this ID
      ingredient.remove("_id");

      // Generate a new ID for the actual database
      ingredient["_id"] = await db?.insert("store_ingredients", ingredient);
      backupIDToActualID[oldId] = ingredient["_id"] as int;

      // Add the store ingredient to the actual store ingredients
      var ingredientObject = StoreIngredient.fromMap(ingredient);
      ingredientFromID[ingredient["_id"] as int] = ingredientObject;
      returnedIngredients.add(ingredientObject);
    }

    List<Map<String, Object?>> recipes = await backupDb.query("recipes", columns: [
      "_id",
      "name",
      "expected_servings",
      "url",
      "thumbnail",
    ]);
    recipes = List<Map<String, dynamic>>.generate(recipes.length, (index) => Map<String, dynamic>.from(recipes[index]),
        growable: true);

    List<Recipe> returnedRecipes = [];

    for (var recipe in recipes) {
      List<Map<String, Object?>> recipeIngredients = await backupDb.query("recipe_ingredients",
          columns: [
            "recipe",
            "volume_type",
            "volume_quantity",
            "store_ingredient",
          ],
          where: "recipe = ?",
          whereArgs: [recipe["_id"] as int]);
      recipeIngredients = List<Map<String, dynamic>>.generate(
          recipeIngredients.length, (index) => Map<String, dynamic>.from(recipeIngredients[index]),
          growable: true);

      recipe.remove("_id");
      // Finally add the recipe to our database
      recipe["_id"] = await db?.insert("recipes", recipe);

      List<Ingredient> recipeIngredientObjects = [];
      for (var recipeIngredient in recipeIngredients) {
        recipeIngredient["recipe"] = recipe["_id"];
        recipeIngredient["store_ingredient"] = backupIDToActualID[recipeIngredient["store_ingredient"] as int];

        recipeIngredient["_id"] = await db?.insert("recipe_ingredients", recipeIngredient);
        recipeIngredientObjects.add(Ingredient.fromMap(recipeIngredient,
            ingredientFromID[recipeIngredient["store_ingredient"] as int] ?? StoreIngredient.createNew()));
      }

      returnedRecipes.add(Recipe.fromMap(recipe, recipeIngredientObjects));
    }

    await backupDb.close();

    return BackupRecipe(returnedIngredients: returnedIngredients, returnedRecipes: returnedRecipes);
  }

  Future<List<Recipe>> getAllRecipes(List<StoreIngredient> ingredients) async {
    List<Map<String, Object?>>? recipes = await db?.query("recipes", columns: [
      "_id",
      "name",
      "expected_servings",
      "url",
      "thumbnail",
      "last_synced",
    ]);

    Map<int, StoreIngredient> ingredientsFromID = <int, StoreIngredient>{};
    for (var ingredient in ingredients) {
      if (ingredient.id != null) {
        ingredientsFromID[ingredient.id!] = ingredient;
      }
    }

    if (recipes == null) {
      return [];
    }

    List<Recipe> returnedRecipes = [];
    for (var recipe in recipes) {
      // Around 100kb
      //print("Size of thumbnail: ${(recipe["thumbnail"] as Uint8List).length}");

      List<Map<String, Object?>>? recipeIngredients = await db?.query("recipe_ingredients",
          columns: [
            "_id",
            "recipe",
            "volume_type",
            "volume_quantity",
            "store_ingredient",
          ],
          where: "recipe = ?",
          whereArgs: [recipe["_id"] as int]);

      if (recipeIngredients != null) {
        var ingredients = recipeIngredients
            .map((map) => Ingredient.fromMap(
                map, ingredientsFromID[map["store_ingredient"] as int] ?? StoreIngredient.createNew()))
            .toList();
        returnedRecipes.add(Recipe.fromMap(recipe, ingredients));
      } else {
        returnedRecipes.add(Recipe.fromMap(recipe, []));
      }
    }

    return returnedRecipes;
  }

  Future<void> deleteRecipe(Recipe recipe) async {
    await db?.delete("recipes", where: "_id = ?", whereArgs: [recipe.id]);
    await db?.delete("recipe_ingredients", where: "recipe = ?", whereArgs: [recipe.id]);
  }

  Future<void> updateRecipe(Recipe recipe) async {
    var recipeMap = recipe.toMap();
    await db?.update("recipes", recipeMap, where: "_id = ?", whereArgs: [recipe.id]);

    // Intelligently handle all recipe ingredients
    List<int> includedIngredients = <int>[];
    for (var ingredient in recipe.ingredients) {
      var ingredientMap = ingredient.toMap();
      ingredientMap["recipe"] = recipe.id;
      if (ingredient.id == null) {
        ingredient.id = await db?.insert("recipe_ingredients", ingredientMap);
      } else {
        // Just in case it needs updating
        await db?.update("recipe_ingredients", ingredientMap, where: "_id = ?", whereArgs: [ingredient.id]);
      }
      includedIngredients.add(ingredient.id!);
      // Not responsible for store ingredients here
    }

    // Remove unused recipe ingredients
    await db?.delete("recipe_ingredients",
        where: "_id NOT IN (${includedIngredients.join(',')}) AND recipe = ?", whereArgs: [recipe.id]);
  }

/*
  Future<List<StoreIngredient>> getUnsyncedIngredients() async {
    List<Map<String, Object?>>? ingredients = await db?.query("store_ingredients",
        columns: [
          "_id",
          "name",
          "volume_type",
          "volume_quantity",
          "price",
          "thumbnail",
        ],
        where: "last_synced IS NULL");

    return ingredients == null ? [] : ingredients.map((map) => StoreIngredient.fromMap(map)).toList();
  }

  Future<List<Recipe>> getUnsyncedRecipes() async {
    List<Map<String, Object?>>? recipes = await db?.query("recipes",
        columns: [
          "_id",
          "name",
          "expected_servings",
          "url",
          "thumbnail",
        ],
        where: "last_synced IS NULL");

    if (recipes == null) {
      return [];
    }

    List<Recipe> returnedRecipes = [];
    for (var recipe in recipes) {
      List<Map<String, Object?>>? recipeIngredients = await db?.query("recipe_ingredients",
          columns: [
            "_id",
            "recipe",
            "volume_type",
            "volume_quantity",
            "store_ingredient",
          ],
          where: "recipe = ?",
          whereArgs: [recipe["_id"] as int]);

      if (recipeIngredients != null) {
        var ingredients = recipeIngredients.map((map) {
          var placeholder = StoreIngredient.createNew();
          // Placeholder for ID only
          placeholder.id = map["_id"] as int;
          return Ingredient.fromMap(map, placeholder);
        }).toList();
        returnedRecipes.add(Recipe.fromMap(recipe, ingredients));
      } else {
        returnedRecipes.add(Recipe.fromMap(recipe, []));
      }
    }
    return returnedRecipes;
  }
  */

  Future<void> syncIngredient(StoreIngredient ingredient) async {
    await db?.update("store_ingredients", {"last_synced": ingredient.lastSynced},
        where: "_id = ?", whereArgs: [ingredient.id]);
  }

  Future<void> syncRecipe(Recipe recipe) async {
    await db?.update("recipes", {"last_synced": recipe.lastSynced}, where: "_id = ?", whereArgs: [recipe.id]);
  }

  Future close() async => db?.close();
}
*/