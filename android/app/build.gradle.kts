plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.alefinot.dashboardpp"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.alefinot.dashboardpp"
        minSdk = 26
        targetSdk = 35
        versionCode = 2
        versionName = "1.0.0"
    }

    // Distribution decision (see Dashboard-Android-App-Implementation-Plan.md §15):
    // we ship the DEBUG build sideloaded — no signingConfigs. The debug keystore
    // is machine-local; keep building from the same machine and bump versionCode
    // on every shared APK.
    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    buildFeatures {
        compose = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    // Compose (pinned via BOM → ui 1.7.5, material3 1.3.1)
    implementation(platform("androidx.compose:compose-bom:2024.11.00"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.material3:material3")
    debugImplementation("androidx.compose.ui:ui-tooling")
    debugImplementation("androidx.compose.ui:ui-tooling-preview")

    implementation("androidx.core:core:1.13.1")
    implementation("androidx.activity:activity-compose:1.9.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.4")
    implementation("androidx.datastore:datastore-preferences:1.1.3")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
}
