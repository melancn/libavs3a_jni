pluginManagement { repositories { google(); mavenCentral(); gradlePluginPortal() } }
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
        maven {
            name = "sdkUnderTest"
            url = uri("build/ci-maven")
            content { includeGroup("com.inlz.avs3a") }
        }
    }
}
rootProject.name = "libavs3a_jni"
include(":sdk", ":smoke-test")
