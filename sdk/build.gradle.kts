plugins {
    id("com.android.library")
    id("maven-publish")
}
val sdkVersion = providers.gradleProperty("sdkVersion").getOrElse("0.1.0-SNAPSHOT")
val sourceCommit = providers.gradleProperty("sourceCommit").getOrElse("uncommitted")
val ndkPin = providers.gradleProperty("androidNdkVersion").getOrElse("27.2.12479018")
val cmakePin = providers.gradleProperty("androidCmakeVersion").getOrElse("3.22.1")
require(sdkVersion.matches(Regex("[0-9]+\\.[0-9]+\\.[0-9]+(?:[-.][A-Za-z0-9]+)*")))
require(sourceCommit == "uncommitted" || sourceCommit.matches(Regex("[0-9a-f]{40}")))
require(ndkPin == "27.2.12479018" && cmakePin == "3.22.1")
group = "com.inlz.avs3a"
version = sdkVersion

android {
    namespace = "com.inlz.avs3a"
    compileSdk = 36
    ndkVersion = ndkPin
    defaultConfig {
        minSdk = 24
        ndk { abiFilters += setOf("arm64-v8a", "armeabi-v7a") }
        consumerProguardFiles("consumer-rules.pro")
        buildConfigField("int", "API_CONTRACT_VERSION", "2")
        buildConfigField("String", "SDK_VERSION", "\"$sdkVersion\"")
        buildConfigField("String", "SOURCE_COMMIT", "\"$sourceCommit\"")
        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_static",
                    "-DAVS3A_SDK_VERSION=$sdkVersion",
                    "-DAVS3A_SOURCE_COMMIT=$sourceCommit",
                )
            }
        }
    }
    buildFeatures { buildConfig = true }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    flavorDimensions += "distribution"
    productFlavors {
        create("bridge") {
            dimension = "distribution"
            buildConfigField("boolean", "BUNDLED_VENDOR", "false")
            externalNativeBuild.cmake.arguments +=
                "-DAVS3A_UNSTRIPPED_DIR=${rootProject.layout.buildDirectory.get().asFile.invariantSeparatorsPath}/unstripped/bridge"
        }
        create("full") {
            dimension = "distribution"
            buildConfigField("boolean", "BUNDLED_VENDOR", "true")
            externalNativeBuild.cmake.arguments +=
                "-DAVS3A_UNSTRIPPED_DIR=${rootProject.layout.buildDirectory.get().asFile.invariantSeparatorsPath}/unstripped/full"
        }
    }
    externalNativeBuild {
        cmake {
            path = rootProject.file("native/CMakeLists.txt")
            version = cmakePin
        }
    }
    packaging { jniLibs.keepDebugSymbols += "**/libavs3a_decoder.so" }
    publishing {
        singleVariant("bridgeRelease") { withSourcesJar() }
        singleVariant("fullRelease") { withSourcesJar() }
    }
}
dependencies { testImplementation("junit:junit:4.13.2") }

// The script must verify staged bytes AND both ABI/dialect readiness.
// Do not touch vendor files when only bridge tasks are requested.
val verifyFullInputs by tasks.registering(Exec::class) {
    workingDir = rootProject.projectDir
    commandLine("python3", "ci/verify-vendor-inputs.py",
        "--lock", "vendor/manifest.lock.json", "--staged-root", ".",
        "--require-abi-ready", "native/vendor/abi-contract.json",
        "--require-dialect-ready", "native/vendor/frame-dialect.json")
}
tasks.matching { it.name == "preFullDebugBuild" || it.name == "preFullReleaseBuild" }
    .configureEach { dependsOn(verifyFullInputs) }

afterEvaluate {
    publishing {
        publications {
            create<MavenPublication>("bridgeRelease") {
                from(components["bridgeRelease"])
                groupId = "com.inlz.avs3a"
                artifactId = "avs3a-sdk-bridge"
                version = sdkVersion
            }
            create<MavenPublication>("fullRelease") {
                from(components["fullRelease"])
                groupId = "com.inlz.avs3a"
                artifactId = "avs3a-sdk"
                version = sdkVersion
            }
        }
        repositories {
            maven {
                name = "ci"
                url = rootProject.layout.buildDirectory.dir("ci-maven").get().asFile.toURI()
            }
        }
    }
}
