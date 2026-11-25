package com.samsung.android.aicore;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;

// DressRoom (the lock screen wallpaper editor) asks com.samsung.android.aicore.feature
// "featureSupportRequest" for FEATURE_WALLPAPER and FEATURE_DOWNLOAD before it shows the Weather
// effect. The effect itself is drawn by SpriteWallpaper and never calls AI Core.
// constVersion: 0 = not available, >= 1 = available since that version.
public final class FeatureProvider extends ContentProvider {
    @Override public boolean onCreate() { return true; }

    @Override public Bundle call(String method, String arg, Bundle extras) {
        Bundle b = new Bundle();
        boolean on = "featureSupportRequest".equals(method)
                && ("FEATURE_WALLPAPER".equals(arg) || "FEATURE_DOWNLOAD".equals(arg));
        b.putInt("constVersion", on ? 1 : 0);
        return b;
    }

    @Override public Cursor query(Uri u, String[] p, String s, String[] a, String o) { return null; }
    @Override public String getType(Uri u) { return null; }
    @Override public Uri insert(Uri u, ContentValues v) { return null; }
    @Override public int delete(Uri u, String s, String[] a) { return 0; }
    @Override public int update(Uri u, ContentValues v, String s, String[] a) { return 0; }
}
