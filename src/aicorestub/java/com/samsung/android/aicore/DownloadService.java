package com.samsung.android.aicore;

import android.app.Service;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import android.util.Log;

// Before it applies the Weather effect, DressRoom asks AI Core (visual.intent.action.BIND_DOWNLOAD_SERVICE,
// IDownloadService transaction 3: checkAndDownload(String model, boolean, boolean) -> int) whether the
// "visionlvm" model is there, and shows "an add-on needs to be downloaded" unless the answer is 0.
// The effect itself never uses that model (it only feeds the AI-generated weather images, which the
// S23 can't run), so report every model as present. The client treats a raw 0 as "no such
// interface" and subtracts 10000 from results above 9000, hence 10000.
public final class DownloadService extends Service {
    static final String DESC = "com.samsung.android.visual.ai.sdkcommon.IDownloadService";

    private final Binder binder = new Binder() {
        { attachInterface(null, DESC); }

        @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
            if (code != 3) return super.onTransact(code, data, reply, flags);
            data.enforceInterface(DESC);
            String model = data.readString();
            Log.i("S23AiCoreStub", "checkAndDownload(" + model + "): reporting present");
            reply.writeNoException();
            reply.writeInt(10000);
            return true;
        }
    };

    @Override public IBinder onBind(Intent intent) { return binder; }
}
