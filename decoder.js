var cursor = 0;
var buffer;

// TTN v3
function decodeUplink(input) {
    return {
      data: Decode(input.fPort, input.bytes, 0),
      warnings: [],
      errors: []
    };
  }

// ChirpStack
function Decode(port, bytes, variables) {

    buffer = bytes;

    var header = u8();
    var battery_voltage = u8();
    var temperature = s16();
    var humidity = u8();
    var pressure = u16();
    var tvoc = u16();
    var co2 = u16();

    return {
        states: {
            header: header,
            battery_voltage: battery_voltage === 0xffff ? null : battery_voltage / 10,
            temperature: temperature === 0xffff ? null : temperature / 10,
            humidity: humidity === 0xff ? null : humidity / 2,
            pressure: pressure === 0xffff ? null : pressure * 2,
            tvoc: tvoc === 0xffff ? null : tvoc,
            co2: co2 === 0xffff ? null : co2,
        }
    };
}

function s8() {
    var value = buffer.slice(cursor);
    value = value[0];
    if ((value & (1 << 7)) > 0) {
        value = (~value & 0xff) + 1;
        value = -value;
    }
    cursor = cursor + 1;
    return value;
}

function u8() {
    var value = buffer.slice(cursor);
    value = value[0];
    cursor = cursor + 1;
    return value;
}

function s16() {
    var value = buffer.slice(cursor);
    value = value[0] | value[1] << 8;
    if ((value & (1 << 15)) > 0) {
        value = (~value & 0xffff) + 1;
        value = -value;
    }
    cursor = cursor + 2;
    return value;
}

function u16() {
    var value = buffer.slice(cursor);
    value = value[0] | value[1] << 8;
    cursor = cursor + 2;
    return value;
}

function u32() {
    var value = buffer.slice(cursor);
    value = value[0] | value[1] << 8 | value[2] << 16 | value[3] << 24;
    cursor = cursor + 4;
    return value;
}

function s32() {
    var value = buffer.slice(cursor);
    value = value[0] | value[1] << 8 | value[2] << 16 | value[3] << 24;
    if ((value & (1 << 31)) > 0) {
        value = (~value & 0xffffffff) + 1;
        value = -value;
    }
    cursor = cursor + 4;
    return value;
}

if (false) {
    var hex = "013f32ff6feb010000ffff"
    var buf = Buffer.from(hex, 'hex')
    console.log(JSON.stringify(Decode(1, buf, 0)));
}
