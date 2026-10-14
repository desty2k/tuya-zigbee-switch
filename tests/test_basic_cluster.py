import datetime

from tests.conftest import Device
from tests.zcl_consts import (
    ZCL_ATTR_BASIC_APP_VER,
    ZCL_ATTR_BASIC_DATE_CODE,
    ZCL_ATTR_BASIC_HW_VER,
    ZCL_ATTR_BASIC_MFR_NAME,
    ZCL_ATTR_BASIC_MODEL_ID,
    ZCL_ATTR_BASIC_POWER_SOURCE,
    ZCL_ATTR_BASIC_STACK_VER,
    ZCL_ATTR_BASIC_SW_BUILD_ID,
    ZCL_ATTR_BASIC_ZCL_VER,
    ZCL_CLUSTER_BASIC,
)


def test_basic_cluster_is_identity_only(device: Device) -> None:
    attributes = (
        ZCL_ATTR_BASIC_ZCL_VER,
        ZCL_ATTR_BASIC_APP_VER,
        ZCL_ATTR_BASIC_STACK_VER,
        ZCL_ATTR_BASIC_HW_VER,
        ZCL_ATTR_BASIC_MFR_NAME,
        ZCL_ATTR_BASIC_MODEL_ID,
        ZCL_ATTR_BASIC_DATE_CODE,
        ZCL_ATTR_BASIC_POWER_SOURCE,
        ZCL_ATTR_BASIC_SW_BUILD_ID,
    )
    for attribute in attributes:
        assert device.read_zigbee_attr(1, ZCL_CLUSTER_BASIC, attribute) is not None


def test_basic_cluster_identity_matches_composition(device: Device,
                                                    device_config: str) -> None:
    manufacturer, model = device_config.split(";")[:2]
    assert device.read_zigbee_attr(1, ZCL_CLUSTER_BASIC,
                                   ZCL_ATTR_BASIC_MFR_NAME) == manufacturer
    assert device.read_zigbee_attr(1, ZCL_CLUSTER_BASIC,
                                   ZCL_ATTR_BASIC_MODEL_ID) == model


def test_basic_cluster_date_code(device: Device) -> None:
    value = device.read_zigbee_attr(1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_DATE_CODE)
    assert datetime.datetime.strptime(value, "%Y%m%d")
