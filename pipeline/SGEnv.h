#ifndef SGENV_H
#define SGENV_H

#define BM_ENV_PREFIX "SGSERVICE_"
// export SGSERVICE_USE_DEVICE="": use all available devices
// export SGSERVICE_USE_DEVICE="1 2": use device_id=1, device_id=2
#define BM_USE_DEVICE (BM_ENV_PREFIX "USE_DEVICE")

#define BM_LOG_LEVEL (BM_ENV_PREFIX "LOG_LEVEL")

#endif // SGENV_H
