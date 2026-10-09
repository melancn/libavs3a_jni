package com.inlz.avs3a;

import java.io.InputStream;
import java.io.IOException;

@FunctionalInterface
public interface ModelSource {
    InputStream open() throws IOException;
}
