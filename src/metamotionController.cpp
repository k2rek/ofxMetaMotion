//
//  metamotionController.cpp
//  Created by Mach1 on 01/28/21.
//
//  References can be found at https://mbientlab.com/cppdocs/latest/index.html
//

#include "metamotionController.h"
#include "ofMain.h"

using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::system_clock;
using std::chrono::time_point;

metamotionController::metamotionController() {

}

metamotionController::~metamotionController() {
    disconnectDevice();
}

//----------------------------------------------------- setup.
void metamotionController::setup() {
    //getDeviceIDs();
    nativeble.setup();
    resetOrientation();
    search();
}

void metamotionController::search() {
    if (!nativeble.connected){
        isConnected = false;
        if (nativeble.devices.size() < 1) { // if there are no found devices search again
            nativeble.rescanDevices();
        } else if (nativeble.devices.size() > 0){ // if there are found devices
            metaMotionDeviceIndex = nativeble.findMetaMotionDevice(); // store autofound index
            if (metaMotionDeviceIndex == -1){ // but they are not MetaMotion search again
                nativeble.listDevices();
                nativeble.rescanDevices();
            } else if (metaMotionDeviceIndex > -1) { // connect to found device in case the above didnt work
                nativeble.connect(metaMotionDeviceIndex);
                
                // setup meta motion
                MblMwBtleConnection btleConnection;
                btleConnection.context = this;
                btleConnection.write_gatt_char = write_gatt_char;
                btleConnection.read_gatt_char = read_gatt_char;
                btleConnection.enable_notifications = enable_char_notify;
                btleConnection.on_disconnect = on_disconnect;
                board = mbl_mw_metawearboard_create(&btleConnection);
                mbl_mw_metawearboard_initialize(board, this, [](void* context, MblMwMetaWearBoard* board, int32_t status) -> void {
                    auto dev_info = mbl_mw_metawearboard_get_device_information(board);
                    std::cout << "firmware revision number = " << dev_info->firmware_revision << std::endl;
                    std::cout << "model = " << mbl_mw_metawearboard_get_model(board) << std::endl;
                    auto *wrapper = static_cast<metamotionController *>(context);
                    wrapper->enable_fusion_sampling(wrapper->board);
                    wrapper->get_current_power_status(wrapper->board);
                });
                isConnected = true;
            }
        }
    }
}

void metamotionController::update(){
    if(nativeble.connected){ // when connected section
        angle[0] = outputEuler[0];
        angle[1] = outputEuler[1];
        angle[2] = outputEuler[2];
        /* Debug
        std::cout << "4: " << outputEuler[3];
        std::cout << "1: " << outputEuler[0];
        std::cout << "2: " << outputEuler[1];
        std::cout << "3: " << outputEuler[2] << std::endl;
         */
    }
}

void metamotionController::disconnectDevice() {
    if (isConnected){
        mbl_mw_metawearboard_free(board);
    }
    isConnected = false;
    nativeble.exit();
}

void metamotionController::data_printer(void* context, const MblMwData* data) {
    // Print data as 2 digit hex values
    uint8_t* data_bytes = (uint8_t*) data->value;
    string bytes_str("[");
    char buffer[5];
    for (uint8_t i = 0; i < data->length; i++) {
        if (i) {
            bytes_str += ", ";
        }
        //sprintf(buffer, "0x%02x", data_bytes[i]);
        bytes_str += buffer;
    }
    bytes_str += "]";

    // Format time as YYYYMMDD-HH:MM:SS.LLL
    time_point<system_clock> then(milliseconds(data->epoch));
    auto time_c = system_clock::to_time_t(then);
    auto rem_ms= data->epoch % 1000;

    std::cout << "{timestamp: " << put_time(localtime(&time_c), "%Y%m%d-%T") << "." << rem_ms << ", "
        << "type_id: " << data->type_id << ", "
        << "bytes: " << bytes_str.c_str() << "}"
        << std::endl;
}

void metamotionController::configure_sensor_fusion(MblMwMetaWearBoard* board) {
    // set fusion mode to ndof (n degress of freedom)
    mbl_mw_sensor_fusion_set_mode(board, MBL_MW_SENSOR_FUSION_MODE_NDOF);
    // set acceleration range to +/-16G, note accelerometer is configured here
    mbl_mw_sensor_fusion_set_acc_range(board, MBL_MW_SENSOR_FUSION_ACC_RANGE_16G);
    // set gyro range to 2000 DPS
    mbl_mw_sensor_fusion_set_gyro_range(board, MBL_MW_SENSOR_FUSION_GYRO_RANGE_2000DPS);
    // write changes to the board
    mbl_mw_sensor_fusion_write_config(board);
    
    // set the tx power as high as allowed
    mbl_mw_settings_set_tx_power(board, 4);
}

void metamotionController::get_current_power_status(MblMwMetaWearBoard* board) {
    auto power_status = mbl_mw_settings_get_power_status_data_signal(board);
    mbl_mw_datasignal_subscribe(power_status, this, [](void* context, const MblMwData* data) -> void {
        auto *wrapper = static_cast<metamotionController *>(context);
        std::cout << "Power Status: " << data << std::endl;
    });
    
    auto charge_status = mbl_mw_settings_get_charge_status_data_signal(board);
    mbl_mw_datasignal_subscribe(charge_status, this, [](void* context, const MblMwData* data) -> void {
        auto *wrapper = static_cast<metamotionController *>(context);
        std::cout << "Charge Status: " << data << std::endl;
    });
}

void metamotionController::enable_fusion_sampling(MblMwMetaWearBoard* board) {
    // Write the config to the sensor
    configure_sensor_fusion(board);
    
    auto fusion_signal = mbl_mw_sensor_fusion_get_data_signal(board, MBL_MW_SENSOR_FUSION_DATA_EULER_ANGLE);
    
    mbl_mw_datasignal_subscribe(fusion_signal, this, [](void* context, const MblMwData* data) -> void {
        auto *wrapper = static_cast<metamotionController *>(context);
        
        auto euler = (MblMwEulerAngles*)data->value;
        if (wrapper->bUseMagnoHeading){ // externally set use of magnometer to correct IMU or not
            wrapper->outputEuler[0] = euler->heading;
            wrapper->outputEuler[3] = euler->yaw;
        } else {
            wrapper->outputEuler[0] = euler->yaw;
            wrapper->outputEuler[3] = euler->heading;
        }
        wrapper->outputEuler[1] = euler->pitch;
        wrapper->outputEuler[2] = euler->roll;
        //printf("(%.3f, %.3f, %.3f)\n", euler->yaw, euler->pitch, euler->roll);
    });
    
    // Start
    mbl_mw_sensor_fusion_enable_data(board, MBL_MW_SENSOR_FUSION_DATA_EULER_ANGLE);
    mbl_mw_sensor_fusion_start(board);
    enable_led(board);
}

void metamotionController::enable_led(MblMwMetaWearBoard* board) {
    //MblMwLedPattern pattern = { 16, 0, 150, 250, 150, 1000, 0, 500 };
    MblMwLedPattern pattern;
    mbl_mw_led_load_preset_pattern(&pattern, MBL_MW_LED_PRESET_PULSE);
    mbl_mw_led_write_pattern(board, &pattern, MBL_MW_LED_COLOR_RED);
    mbl_mw_led_play(board);
}

void metamotionController::disable_fusion_sampling(MblMwMetaWearBoard* board) {
    auto fusion_signal = mbl_mw_sensor_fusion_get_data_signal(board, MBL_MW_SENSOR_FUSION_DATA_EULER_ANGLE);
    mbl_mw_datasignal_unsubscribe(fusion_signal);
    mbl_mw_sensor_fusion_stop(board);
    // stop the LED pattern from playing
    mbl_mw_led_stop(board);
}

void metamotionController::calibration_mode(MblMwMetaWearBoard* board) {
    
}

void metamotionController::resetOrientation() {
    for(int i=0; i< 3; i++){
        angle_shift[i] = 0;
    }
}

void metamotionController::recenter() {
    float* swpAngle = getAngle();
    angle_shift[0] = angle_shift[0] - swpAngle[0];
    angle_shift[1] = angle_shift[1] - swpAngle[1];
    angle_shift[2] = angle_shift[2] - swpAngle[2];
}

float* metamotionController::getAngle() {
    float* calculated_angle = new float[3];
        
    calculated_angle[0] = angle[0] + angle_shift[0];
    calculated_angle[1] = angle[1] + angle_shift[1];
    calculated_angle[2] = angle[2] + angle_shift[2];
    
    return calculated_angle;
}

string HighLow2Uuid(const uint64_t high, const uint64_t low){
    uint8_t *u_h = (uint8_t *)&(high);
    uint8_t *u_l = (uint8_t *)&(low);

    char UUID[38];
    std::sprintf(UUID, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
            u_h[7], u_h[6], u_h[5], u_h[4], u_h[3], u_h[2], u_h[1], u_h[0],
            u_l[7], u_l[6], u_l[5], u_l[4], u_l[3], u_l[2], u_l[1], u_l[0]
            );
    
    return string(UUID);
}

void metamotionController::read_gatt_char(void *context, const void *caller, const MblMwGattChar *characteristic,
                                          MblMwFnIntVoidPtrArray handler) {
    auto *wrapper = static_cast<metamotionController *>(context);

    wrapper->nativeble.ble.read(HighLow2Uuid(characteristic->service_uuid_high, characteristic->service_uuid_low), HighLow2Uuid(characteristic->uuid_high, characteristic->uuid_low), [&, handler, caller](const uint8_t* data, uint32_t length) {
        handler(caller,data,length);
   });
}


void metamotionController::write_gatt_char(void *context, const void *caller, MblMwGattCharWriteType writeType,
                                          const MblMwGattChar *characteristic, const uint8_t *value, uint8_t length){
    auto *wrapper = static_cast<metamotionController *>(context);
    wrapper->nativeble.ble.write_command(HighLow2Uuid(characteristic->service_uuid_high, characteristic->service_uuid_low), HighLow2Uuid(characteristic->uuid_high, characteristic->uuid_low), std::string((char*)value, int(length)));
}


void metamotionController::enable_char_notify(void *context, const void *caller, const MblMwGattChar *characteristic,
                                             MblMwFnIntVoidPtrArray handler, MblMwFnVoidVoidPtrInt ready) {
   auto *wrapper = static_cast<metamotionController *>(context);
    wrapper->nativeble.ble.notify(HighLow2Uuid(characteristic->service_uuid_high, characteristic->service_uuid_low), HighLow2Uuid(characteristic->uuid_high, characteristic->uuid_low), [&,handler,caller](const uint8_t* data, uint32_t length) {
        handler(caller,data,length);
    });
    ready(caller, MBL_MW_STATUS_OK);
}

void metamotionController::on_disconnect(void *context, const void *caller, MblMwFnVoidVoidPtrInt handler) {
    auto *wrapper = static_cast<metamotionController *>(context);
    handler(caller, MBL_MW_STATUS_OK);
}
