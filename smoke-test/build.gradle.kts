plugins {
    id("com.android.application")
}
val sdkVersion = providers.gradleProperty("sdkVersion").getOrElse("0.1.0-SNAPSHOT")
val testAbi = providers.gradleProperty("testAbi").getOrElse("arm64-v8a")
require(testAbi in setOf("arm64-v8a", "armeabi-v7a"))
android {
    namespace = "com.inlz.avs3a.smoke"
    compileSdk = 36
    defaultConfig {
        applicationId = "com.inlz.avs3a.smoke"
        minSdk = 24
        targetSdk = 36
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk { abiFilters += testAbi }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    packaging {
        jniLibs.keepDebugSymbols += setOf("**/libavs3a_jni.so", "**/libavs3a_decoder.so")
    }
}
dependencies {
    implementation("com.inlz.avs3a:avs3a-sdk:$sdkVersion")
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
}
