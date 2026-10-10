package com.inlz.avs3a.demo.player;

import androidx.media3.extractor.*;
import com.inlz.avs3a.demo.media3.mp4.Mp4Extractor;
import com.inlz.avs3a.demo.media3.Avs3Extractor;
import java.util.ArrayList;

public final class DemoExtractorsFactory implements ExtractorsFactory {
    @Override public Extractor[] createExtractors() {
        ArrayList<Extractor> result = new ArrayList<>();
        result.add(new Mp4Extractor());
        result.add(new Avs3Extractor());
        for (Extractor extractor : new DefaultExtractorsFactory().createExtractors()) {
            if (!(extractor instanceof androidx.media3.extractor.mp4.Mp4Extractor)) result.add(extractor);
        }
        return result.toArray(new Extractor[0]);
    }
}
