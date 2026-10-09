package com.inlz.avs3a.smoke;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;

public class SmokeActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        TextView tv = new TextView(this);
        tv.setText("AVS3A SDK Smoke Test");
        setContentView(tv);
    }
}
