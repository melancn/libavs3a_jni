plugins { id("com.android.application") }
val demoVersion = providers.gradleProperty("sdkVersion").getOrElse("0.1.0-SNAPSHOT")
val demoVersionCode = providers.gradleProperty("demoVersionCode").getOrElse("1").toInt()
require(demoVersion.matches(Regex("[0-9]+\\.[0-9]+\\.[0-9]+(?:[-.][A-Za-z0-9]+)*")))
require(demoVersionCode in 1..2100000000)

android {
    namespace = "com.inlz.avs3a.demo"
    compileSdk = 36
    defaultConfig {
        applicationId = "com.inlz.avs3a.demo"
        minSdk = 24
        targetSdk = 36
        versionCode = demoVersionCode
        versionName = demoVersion
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk { abiFilters += setOf("arm64-v8a", "armeabi-v7a") }
    }
    flavorDimensions += "distribution"
    productFlavors {
        create("bridge") { dimension = "distribution"; applicationIdSuffix = ".bridge" }
        create("full") { dimension = "distribution" }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    // RuntimeVendorVerifier uses nativeLibraryDir; keep the vendor as a real file.
    packaging {
        jniLibs.useLegacyPackaging = true
        // Preserve locked bytes on CI as well as local builds. Runtime SHA checks the vendor SO.
        jniLibs.keepDebugSymbols += setOf("**/libavs3a_decoder.so", "**/libavs3a_jni.so")
    }
    testOptions { unitTests.isIncludeAndroidResources = true }
}

val media3Version = "1.11.1" // Must match the isolated MP4 parser fork; see third_party/README.md.
dependencies {
    implementation(project(":sdk"))
    implementation("androidx.annotation:annotation:1.9.1")
    compileOnly("com.google.code.findbugs:jsr305:3.0.2")
    implementation("androidx.media3:media3-exoplayer:$media3Version")
    implementation("androidx.media3:media3-ui:$media3Version")
    implementation("androidx.media3:media3-extractor:$media3Version")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.robolectric:robolectric:4.16.1")
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
}

// Optional private target sample; never checked into the repository or required by public CI.
tasks.withType<Test>().configureEach {
    providers.gradleProperty("avs3TestMp4").orNull?.let { systemProperty("avs3.test.mp4", it) }
}
