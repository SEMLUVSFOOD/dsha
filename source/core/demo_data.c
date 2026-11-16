// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// States used by the offline demo mode. Generated from
// tests/fixtures/states.json (only the attributes used by DSHA are kept).

#include "demo_data.h"

const char demo_states_json[] =
    "[{\"entity_id\":\"sun.sun\",\"state\":\"above_horizon\",\"attributes\":{\"friendl"
    "y_name\":\"Sun\"}},{\"entity_id\":\"light.living_room\",\"state\":\"on\",\"attribu"
    "tes\":{\"supported_color_modes\":[\"color_temp\",\"hs\"],\"brightness\":191,\"fr"
    "iendly_name\":\"Living room ceiling\"}},{\"entity_id\":\"light.hallway\",\"sta"
    "te\":\"off\",\"attributes\":{\"supported_color_modes\":[\"onoff\"],\"friendly_na"
    "me\":\"Hallway\"}},{\"entity_id\":\"light.k\\u00fcche\",\"state\":\"off\",\"attribu"
    "tes\":{\"supported_color_modes\":[\"brightness\"],\"brightness\":null,\"friend"
    "ly_name\":\"K\\u00fcche \\u00e9clairage \\u2728\"}},{\"entity_id\":\"switch.cof"
    "fee_machine\",\"state\":\"on\",\"attributes\":{\"friendly_name\":\"Coffee machin"
    "e\"}},{\"entity_id\":\"input_boolean.guest_mode\",\"state\":\"off\",\"attributes"
    "\":{\"friendly_name\":\"Guest mode\"}},{\"entity_id\":\"climate.thermostat\",\"s"
    "tate\":\"heat\",\"attributes\":{\"min_temp\":7,\"max_temp\":35,\"target_temp_ste"
    "p\":0.5,\"current_temperature\":20.5,\"temperature\":21.5,\"hvac_action\":\"he"
    "ating\",\"friendly_name\":\"Thermostat\"}},{\"entity_id\":\"cover.garage\",\"sta"
    "te\":\"closed\",\"attributes\":{\"current_position\":0,\"device_class\":\"garage"
    "\",\"friendly_name\":\"Garage door\"}},{\"entity_id\":\"lock.front_door\",\"stat"
    "e\":\"locked\",\"attributes\":{\"friendly_name\":\"Front door\"}},{\"entity_id\":"
    "\"media_player.kitchen_speaker\",\"state\":\"playing\",\"attributes\":{\"volume"
    "_level\":0.35,\"media_title\":\"Bohemian \\\"Rhapsody\\\"\",\"friendly_name\":\"Ki"
    "tchen speaker\"}},{\"entity_id\":\"sensor.outside_temperature\",\"state\":\"7."
    "849999\",\"attributes\":{\"unit_of_measurement\":\"\\u00b0C\",\"device_class\":\""
    "temperature\",\"friendly_name\":\"Outside temperature\"}},{\"entity_id\":\"sen"
    "sor.humidity\",\"state\":\"64\",\"attributes\":{\"unit_of_measurement\":\"%\",\"de"
    "vice_class\":\"humidity\",\"friendly_name\":\"Humidity\"}},{\"entity_id\":\"bina"
    "ry_sensor.back_door\",\"state\":\"on\",\"attributes\":{\"device_class\":\"door\","
    "\"friendly_name\":\"Back door\"}},{\"entity_id\":\"binary_sensor.hall_motion\""
    ",\"state\":\"off\",\"attributes\":{\"device_class\":\"motion\",\"friendly_name\":\""
    "Hall motion\"}},{\"entity_id\":\"scene.movie_night\",\"state\":\"2026-11-15T20"
    ":00:00.000000+00:00\",\"attributes\":{\"friendly_name\":\"Movie night\"}},{\"e"
    "ntity_id\":\"script.good_night\",\"state\":\"off\",\"attributes\":{\"friendly_na"
    "me\":\"Good night\"}},{\"entity_id\":\"person.sam\",\"state\":\"home\",\"attribute"
    "s\":{\"friendly_name\":\"Sam\"}},{\"entity_id\":\"weather.home\",\"state\":\"partl"
    "ycloudy\",\"attributes\":{\"temperature\":8.2,\"temperature_unit\":\"\\u00b0C\","
    "\"friendly_name\":\"Forecast Home\"}},{\"entity_id\":\"input_number.target\",\""
    "state\":\"42.0\",\"attributes\":{\"min\":0.0,\"max\":100.0,\"step\":1.0,\"friendly"
    "_name\":\"Target value\"}},{\"entity_id\":\"update.core\",\"state\":\"off\",\"attr"
    "ibutes\":{\"friendly_name\":\"Core update\"}},{\"entity_id\":\"zone.home\",\"sta"
    "te\":\"0\",\"attributes\":{\"friendly_name\":\"Home\"}},{\"entity_id\":\"fan.bedro"
    "om\",\"state\":\"unavailable\",\"attributes\":{\"friendly_name\":\"Bedroom fan\"}"
    "},{\"entity_id\":\"vacuum.robot\",\"state\":\"docked\",\"attributes\":{\"friendly"
    "_name\":\"Robbie\"}},{\"entity_id\":\"automation.lights_at_sunset\",\"state\":\""
    "on\",\"attributes\":{\"friendly_name\":\"Lights at sunset\"}},{\"entity_id\":\"b"
    "utton.restart_router\",\"state\":\"2026-11-01T10:00:00+00:00\",\"attributes\""
    ":{\"device_class\":\"restart\",\"friendly_name\":\"Restart router\"}}]";
