plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "fr.meudo.motoesp32"
    compileSdk = 35

    defaultConfig {
        applicationId = "fr.meudo.motoesp32"
        minSdk = 26
        targetSdk = 35
        versionCode = 11
        versionName = "0.3.6"
    }

    // Clé de signature de TEST, la même à chaque compilation : sans elle, GitHub crée une clé différente
    // à chaque fois et Android refuse la mise à jour (« package en conflit »). Ce n'est pas une clé
    // de publication : elle ne sert qu'à l'APK de test (mot de passe « android », comme toute clé de débogage).
    signingConfigs {
        getByName("debug") {
            storeFile = file("cle-de-test.keystore")
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // Pas de clé de signature dans le dépôt : l'APK « debug » est celui à installer.
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    buildFeatures {
        compose = true
    }
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2024.12.01")
    implementation(composeBom)
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.7")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.7")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.7")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
    // Carte OpenStreetMap (sans clé d'API)
    implementation("org.osmdroid:osmdroid-android:6.1.20")
    debugImplementation("androidx.compose.ui:ui-tooling")

    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20240303")
}
