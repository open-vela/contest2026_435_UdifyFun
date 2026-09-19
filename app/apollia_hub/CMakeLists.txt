# Contest 2026 team 435 (UdifyFun) apollia_hub
if(CONFIG_LVX_USE_DEMO_CONTEST2026_435_APOLLIA_HUB)
  nuttx_add_application(
    NAME apollia_hub
    SRCS apollia_hub_main.c
    STACKSIZE 8192)
endif()
