package com.apollophantom.quicklimit;

import android.Manifest;
import android.app.Activity;
import android.bluetooth.*;
import android.bluetooth.le.*;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.os.*;
import android.view.Gravity;
import android.widget.*;
import java.util.Arrays;
import java.util.UUID;

public class MainActivity extends Activity {
    private static final String ADDRESS = "CD:00:01:03:66:80";
    private static UUID uuid(String s) { return UUID.fromString("0000" + s + "-0000-1000-8000-00805f9b34fb"); }
    private static final UUID SERVICE = uuid("f1f0"), WRITE = uuid("f1f1"), NOTIFY = uuid("f1f2"), CCC = uuid("2902");
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final byte[] prepare = {(byte)0xa5,0,(byte)0xff,0,0,0,0,0x5a};
    // 15 mph rounds to 24 km/h, represented as 240 tenths of km/h.
    private final byte[] command = {1,0x17,0,0x20,0,1,0,0x20,0,1,2,0,(byte)240,0,0};
    private final byte[] expected = {1,0x17,0,0x20,2,0,(byte)240,0,0};
    private TextView status; private BluetoothGatt gatt; private BluetoothLeScanner scanner;
    private BluetoothGattCharacteristic writer; private boolean finished, scanning;
    private final Runnable timeout = () -> reconnect("No response yet. Reconnecting…");
    private void crc(byte[] data) { int c=0xffff; for(int i=0;i<data.length-2;i++){ c ^= data[i]&255; for(int b=0;b<8;b++) c=(c&1)!=0?(c>>>1)^0xa001:c>>>1; } data[data.length-2]=(byte)c; data[data.length-1]=(byte)(c>>>8); }
    @Override public void onCreate(Bundle b) { super.onCreate(b); crc(command); crc(expected);
        LinearLayout box = new LinearLayout(this); box.setBackgroundColor(Color.TRANSPARENT); box.setOrientation(1); box.setGravity(Gravity.CENTER); box.setPadding(40,40,40,40);
        status = new TextView(this); status.setTextColor(Color.WHITE); status.setShadowLayer(4f, 0f, 0f, Color.BLACK); status.setTextSize(20); status.setGravity(Gravity.CENTER); box.addView(status);
        setContentView(box); start(); }
    private void show(String s) { runOnUiThread(() -> status.setText(s)); }
    private void start() { cleanup(); finished=false;
        String[] permissions = Build.VERSION.SDK_INT >= 31 ? new String[]{Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.BLUETOOTH_CONNECT} : new String[]{Manifest.permission.ACCESS_FINE_LOCATION};
        for(String p:permissions) if(checkSelfPermission(p)!=PackageManager.PERMISSION_GRANTED) { requestPermissions(permissions,1); show("Allow Bluetooth access to set the limit."); return; }
        BluetoothManager manager=(BluetoothManager)getSystemService(BLUETOOTH_SERVICE); BluetoothAdapter adapter=manager == null ? null : manager.getAdapter();
        if(adapter==null) { finish("Bluetooth is unavailable on this device."); return; }
        if(!adapter.isEnabled()) { startActivityForResult(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE),2); return; }
        scanner=adapter.getBluetoothLeScanner(); if(scanner==null){finish("Bluetooth scanning is unavailable.");return;}
        show("Looking for scooter…"); scanning=true;
        try { scanner.startScan(scanCallback); handler.postDelayed(() -> { if(scanning) reconnect("Scooter not found. Looking again…"); }, 10000); }
        catch(SecurityException e){ finish("Bluetooth permission is required."); }
    }
    private final ScanCallback scanCallback = new ScanCallback() {
        @Override public void onScanResult(int type, ScanResult result) { try { if(!scanning || !ADDRESS.equalsIgnoreCase(result.getDevice().getAddress())) return; scanning=false; scanner.stopScan(this); show("Connecting…"); gatt=result.getDevice().connectGatt(MainActivity.this,false,callback); handler.postDelayed(timeout,15000); } catch(SecurityException e){finish("Bluetooth permission is required.");} }
        @Override public void onScanFailed(int error){reconnect("Bluetooth scan failed. Trying again…");}
    };
    private final BluetoothGattCallback callback = new BluetoothGattCallback() {
        @Override public void onConnectionStateChange(BluetoothGatt g, int status, int state) { if(finished || g != gatt)return; try { if(status==BluetoothGatt.GATT_SUCCESS && state==BluetoothProfile.STATE_CONNECTED) g.discoverServices(); else if(state==BluetoothProfile.STATE_DISCONNECTED || status!=BluetoothGatt.GATT_SUCCESS) reconnect("Connection lost. Trying again…"); } catch(SecurityException e){finish("Bluetooth permission is required.");} }
        @Override public void onServicesDiscovered(BluetoothGatt g,int s) { if(finished || g != gatt)return; try { BluetoothGattService service=g.getService(SERVICE); writer=service==null?null:service.getCharacteristic(WRITE); BluetoothGattCharacteristic receiver=service==null?null:service.getCharacteristic(NOTIFY); BluetoothGattDescriptor descriptor=receiver==null?null:receiver.getDescriptor(CCC);
            if(s!=0 || writer==null || receiver==null || descriptor==null || (writer.getProperties() & BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE)==0 || !g.setCharacteristicNotification(receiver,true)){reconnect("Scooter service unavailable. Trying again…");return;}
            descriptor.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE); if(!g.writeDescriptor(descriptor)) reconnect("Could not enable responses. Trying again…");
        } catch(SecurityException e){finish("Bluetooth permission is required.");} }
        @Override public void onDescriptorWrite(BluetoothGatt g,BluetoothGattDescriptor d,int s){ if(finished || g != gatt)return; if(s!=0) reconnect("Could not enable responses. Trying again…"); else { show("Setting 15 mph…"); handler.post(() -> send()); } }
        @Override public void onCharacteristicChanged(BluetoothGatt g,BluetoothGattCharacteristic c) { if(g == gatt && !finished) check(c.getValue()); }
        @Override public void onCharacteristicChanged(BluetoothGatt g,BluetoothGattCharacteristic c,byte[] value) { if(g == gatt && !finished) check(value); }
    };
    private void check(byte[] value) { if(value!=null && value.length>=expected.length && Arrays.equals(expected,Arrays.copyOf(value,expected.length))) handler.post(() -> { if(finished)return; finished=true; cleanup(); Toast.makeText(this,"15 mph response received",Toast.LENGTH_LONG).show(); finish(); }); }
    private void send() { if(finished || gatt==null)return; try { writer.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE); writer.setValue(prepare); if(!gatt.writeCharacteristic(writer)){reconnect("Send failed. Trying again…");return;} handler.postDelayed(() -> { if(finished||gatt==null)return; try { writer.setValue(command); if(!gatt.writeCharacteristic(writer)){reconnect("Send failed. Trying again…");return;} handler.postDelayed(this::send,450); } catch(SecurityException e){finish("Bluetooth permission is required.");} },30); } catch(SecurityException e){finish("Bluetooth permission is required.");} }
    private void finish(String message) { handler.post(() -> { if(finished)return; finished=true; cleanup(); show(message); }); }
    private void reconnect(String message) { handler.post(() -> { if(finished)return; cleanup(); show(message); handler.postDelayed(this::start, 1000); }); }
    private void cleanup() { handler.removeCallbacksAndMessages(null); try { if(scanning && scanner!=null) scanner.stopScan(scanCallback); scanning=false; if(gatt!=null){gatt.disconnect();gatt.close();gatt=null;} } catch(SecurityException ignored) {} }
    @Override public void onRequestPermissionsResult(int request,String[] p,int[] results){super.onRequestPermissionsResult(request,p,results); if(request==1){ for(int r:results) if(r!=PackageManager.PERMISSION_GRANTED){finish("Bluetooth permission is required.");return;} start(); }}
    @Override protected void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data); if(request==2){if(result==RESULT_OK)start();else finish("Turn on Bluetooth to continue.");}}
    @Override protected void onDestroy(){finished=true; cleanup();super.onDestroy();}
}
