/**
 * @file px4_daemon_app.c
 * daemon application example for PX4 autopilot
 *
 * @author Example User <mail@example.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <px4_tasks.h>

#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <mathlib/mathlib.h>

#include <px4_config.h>
#include <nuttx/sched.h>

#include <systemlib/err.h>
#include <drivers/drv_hrt.h>

#include <uORB/uORB.h>
#include <uORB/topics/rc_channels.h>
#include <uORB/topics/delivery_signal.h>
#include <uORB/topics/vehicle_global_position.h>
#include <uORB/topics/vehicle_gps_position.h>
#include <uORB/topics/offboard_setpoint.h>
#include <uORB/topics/vehicle_local_position.h>
#include <uORB/topics/home_position.h>

static bool thread_should_exit = false;     /**< daemon exit flag */
static bool thread_running = false;     /**< daemon status flag */
static int get_gps_data_task;             /**< Handle of daemon task / thread */
static int gps_sequence = 0;
//static double CONSTANTS_RADIUS_OF_EARTH = 6371000.0;
//static double pi = 3.14159265358979323846;

struct ref_point {
    double lat_rad;
    double lon_rad;
    double sin_lat;
    double cos_lat;
};

__EXPORT int get_gps_data_main(int argc, char *argv[]);

int get_data_thread_main(int argc, char *argv[]);

//static int wgs_ned(const struct ref_point *ref, double lat, double lon, float *x, float *y);

static void usage(const char *reason);

//int wgs_ned(const struct ref_point *ref, double lat, double lon, float *x, float *y)
//{
//    const double lat_rad = lat * (pi / 180);
//    const double lon_rad = lon * (pi / 180);

//    const double sin_lat = sin(lat_rad);
//    const double cos_lat = cos(lat_rad);

//    const double cos_d_lon = cos(lon_rad - ref->lon_rad);

//    int val = ref->sin_lat * sin_lat + ref->cos_lat * cos_lat * cos_d_lon;
//    const double arg = (val < -1.0) ? -1.0 : ((val > 1.0) ? 1.0 : val);
//    const double c = acos(arg);

//    double k = 1.0;

//    if (fabs(c) > 0) {
//        k = (c / sin(c));
//    }

//    *x = (float32)(k * (ref->cos_lat * sin_lat - ref->sin_lat * cos_lat * cos_d_lon) * CONSTANTS_RADIUS_OF_EARTH);
//    *y = (float32)(k * cos_lat * sin(lon_rad - ref->lon_rad) * CONSTANTS_RADIUS_OF_EARTH);

//    return 0;
//}

static void usage(const char *reason)
{
    if (reason) {
        warnx("%s\n", reason);
    }

    warnx("usage: get_gps_data {start|stop|status|numb} [-p <additional params>]\n\n");
}


int get_gps_data_main(int argc, char *argv[])
{
    if (argc < 2) {
        usage("missing command");
        return 1;
    }

    if (!strcmp(argv[1], "start")) {

        if (thread_running) {
            warnx("daemon already running\n");
            /* this is not an error */
            return 0;
        }

        thread_should_exit = false;
        get_gps_data_task = px4_task_spawn_cmd("get_gps_data",
                         SCHED_DEFAULT,
                         SCHED_PRIORITY_DEFAULT,
                         2000,
                         get_data_thread_main,
                         (argv) ? (char *const *)&argv[2] : (char *const *)NULL);
        return 0;
    }

    if (!strcmp(argv[1], "stop")) {
        thread_should_exit = true;
        return 0;
    }

    if (!strcmp(argv[1], "status")) {
        if (thread_running) {
            warnx("\trunning\n");

        } else {
            warnx("\tnot started\n");
        }

        return 0;
    }

    if (!strcmp(argv[1], "1")) {
        gps_sequence = 1;
        return 0;
    }

    if (!strcmp(argv[1], "2")) {
        gps_sequence = 2;
        return 0;
    }

    if (!strcmp(argv[1], "3")) {
        gps_sequence = 3;
        return 0;
    }


    usage("unrecognized command");
    return 1;

}

int get_data_thread_main(int argc, char *argv[])
{

    warnx("[daemon] starting\n");

    thread_running = true;

    bool get_a = false;
    bool get_b = false;
    bool get_c = false;

    bool close_a = false;
    bool close_b = false;
    bool close_c = false;
    bool is_vxy_zero = false;
    bool is_vz_zero = false;

    int count = 0;
    int count_time = 0;
    int count_drop = 0;
    int token = 1;
    float32 sum_x = 0.0;
    float32 sum_y = 0.0;
    float32 sum_z = 0.0;
    double sum_square = 0.0;

    int rc_channels_sub = orb_subscribe(ORB_ID(rc_channels));
    int vehicle_global_position_sub = orb_subscribe(ORB_ID(vehicle_global_position));
    int vehicle_local_position_sub = orb_subscribe(ORB_ID(vehicle_local_position));
    int home_position_sub = orb_subscribe(ORB_ID(home_position));

    struct rc_channels_s _rc_channels = {};
    struct vehicle_global_position_s _vehicle_global_position = {};
    struct delivery_signal_s _delivery_signal = {};
    struct vehicle_local_position_s local_now = {};
    struct home_position_s _home_position = {};
    struct offboard_setpoint_s _offboard_sp = {};

//    struct vehicle_global_position_s global_a = {};
//    struct vehicle_global_position_s global_b = {};
//    struct vehicle_global_position_s global_c = {};

    struct vehicle_local_position_s local_a = {};
    struct vehicle_local_position_s local_b = {};
    struct vehicle_local_position_s local_c = {};

//    float32 local_b.x = 0.0;
//    float32 local_b.y = 0.0;
//    float32 local_c.x = 0.0;
//    float32 local_c.y = 0.0;
//    float32 local_now.x = 0.0;
//    float32 local_now.y = 0.0;
//    float32 z_home = 0.0;
    float32 vx = 0.0;
    float32 vy = 0.0;
    float32 vz = 0.0;
    float32 vxy_max = 0.0;
    float32 set_high = -7.0;
//    float32 set_threshold_up = -6.5;
//    float32 set_threshold_down = -2.0;

//    struct ref_point ref_a = {};

    _delivery_signal.is_point_b = false;
    _delivery_signal.is_point_c = false;
    _offboard_sp.ignore_alt_hold = true;
    _offboard_sp.ignore_attitude = true;
    _offboard_sp.ignore_position = true;
    _offboard_sp.ignore_velocity = true;
    _offboard_sp.is_idle_sp = false;
    _offboard_sp.is_land_sp = false;
    _offboard_sp.is_local_frame = true;
    _offboard_sp.is_loiter_sp = false;
    _offboard_sp.is_takeoff_sp = false;
    _offboard_sp.timestamp = hrt_absolute_time();

    orb_advert_t delivery_signal_pub = orb_advertise(ORB_ID(delivery_signal), &_delivery_signal);
    orb_advert_t offboard_setpoint_pub = orb_advertise(ORB_ID(offboard_setpoint), &_offboard_sp);

    orb_publish(ORB_ID(offboard_setpoint), offboard_setpoint_pub, &_offboard_sp);
    orb_publish(ORB_ID(delivery_signal), delivery_signal_pub, &_delivery_signal);

    while (!thread_should_exit) {

        bool updated_rcch;
        bool updated_vp_global;
        bool updated_vp_local;
        bool updated_home;

        orb_check(rc_channels_sub, &updated_rcch);
        orb_check(vehicle_global_position_sub, &updated_vp_global);
        orb_check(vehicle_local_position_sub, &updated_vp_local);
        orb_check(home_position_sub, &updated_home);

        if (updated_rcch) {
            orb_copy(ORB_ID(rc_channels), rc_channels_sub, &_rc_channels);
        }

        if (updated_vp_local) {
            orb_copy(ORB_ID(vehicle_local_position), vehicle_local_position_sub, &local_now);
            vx = local_now.vx;
            vy = local_now.vy;
            vz = local_now.vz;
            vxy_max = local_now.vxy_max;

            if (gps_sequence == 1 && !get_a) {
                sum_x = sum_x + local_now.x;
                sum_y = sum_y + local_now.y;
                sum_z = sum_z + local_now.z;
                count++;
                if (count == 10) {
                    local_a.x = sum_x/count;
                    local_a.y = sum_y/count;
                    local_a.z = sum_z/count;
                    get_a = true;
                    count = 0;
                    sum_x = 0.0;
                    sum_y = 0.0;
                    sum_z = 0.0;
                }
                warnx("[gaemon] sequence == 1.\n");
                printf("  a:  x=%6.3f\t", (double)local_a.x);
                printf("y=%6.3f\t", (double)local_a.y);
                printf("z=%6.3f\n", (double)local_a.z);

            }

            if (gps_sequence == 2 && !get_b) {
                sum_x = sum_x + local_now.x;
                sum_y = sum_y + local_now.y;
                sum_z = sum_z + local_now.z;
                count++;
                if (count == 10) {
                    local_b.x = sum_x/count;
                    local_b.y = sum_y/count;
                    local_b.z = sum_z/count;
                    get_b = true;
                    count = 0;
                    sum_x = 0.0;
                    sum_y = 0.0;
                    sum_z = 0.0;
                }
                warnx("[gaemon] sequence == 2.\n");
                printf("vxy_max = %6.3f\n",(double)vxy_max);
            }

            if (gps_sequence == 3 && !get_c) {
                sum_x = sum_x + local_now.x;
                sum_y = sum_y + local_now.y;
                sum_z = sum_z + local_now.z;
                count++;
                if (count == 10) {
                    local_c.x = sum_x/count;
                    local_c.y = sum_y/count;
                    local_c.z = sum_z/count;
                    get_c = true;
                    count = 0;
                    sum_x = 0.0;
                    sum_y = 0.0;
                    sum_z = 0.0;
                }
                warnx("[gaemon] sequence == 3.\n");
            }
        }

        if (updated_home) {
            orb_copy(ORB_ID(home_position), home_position_sub, &_home_position);
        }

        if (updated_vp_global) {
            orb_copy(ORB_ID(vehicle_global_position), vehicle_global_position_sub, &_vehicle_global_position);

//            if (gps_sequence == 1 && !get_a) {
//                sum_lat = sum_lat + _vehicle_global_position.lat;
//                sum_lon = sum_lon + _vehicle_global_position.lon;
//                z_sum = z_sum + _vehicle_local_position.z;
//                count++;
//                if (count == 10) {
//                    global_a.lat = sum_lat/count;
//                    global_a.lon = sum_lon/count;
//                    z_home = z_sum/count;
//                    get_a = true;
//                    count = 0;
//                    sum_lat = 0.0;
//                    sum_lon = 0.0;
//                    z_sum = 0.0;
//                }
//                warnx("[gaemon] sequence == 1.\n");
//            }

//            if (gps_sequence == 2 && !get_b) {
//                sum_lat = sum_lat + _vehicle_global_position.lat;
//                sum_lon = sum_lon + _vehicle_global_position.lon;
//                count++;
//                if (count == 10) {
//                    global_b.lat = sum_lat/count;
//                    global_b.lon = sum_lon/count;
//                    get_b = true;
//                    count = 0;
//                    sum_lat = 0.0;
//                    sum_lon = 0.0;
//                }
//                warnx("[gaemon] sequence == 2.\n");
//                printf("vxy_max = %6.3f\n",(double)vxy_max);
//            }

//            if (gps_sequence == 3 && !get_c) {
//                sum_lat = sum_lat + _vehicle_global_position.lat;
//                sum_lon = sum_lon + _vehicle_global_position.lon;
//                count++;
//                if (count == 10) {
//                    global_c.lat = sum_lat/count;
//                    global_c.lon = sum_lon/count;
//                    get_c = true;
//                    count = 0;
//                    sum_lat = 0.0;
//                    sum_lon = 0.0;
//                }
//                warnx("[gaemon] sequence == 3.\n");
//            }
        }
        /* 显示abc_gps */
//        if (count_time%5 == 0) {
//            switch (gps_sequence) {
//            case 1:
//                printf("glo:  lat_a=%12.9f\t",global_a.lat);
//                printf("lon_a=%12.9f\n",global_a.lon);
//                break;

//            case 2:
//                printf("glo:  lat_b=%12.9f\t",global_b.lat);
//                printf("lon_b=%12.9f\n",global_b.lon);
//                break;

//            case 3:
//                printf("glo:  lat_c=%12.9f\t",global_c.lat);
//                printf("lon_c=%12.9f\n",global_c.lon);
//                break;

//            default:
//                break;

//            }
//            if (count_time > 100)
//                count_time = 0;
//            /* 显示当前位置,home位置*/
//            printf("glo:  lat_n=%12.9f\t", _vehicle_global_position.lat);
//            printf("lon_n=%12.9f\n", _vehicle_global_position.lon);
//            printf("loc:  x=%8.4f\t", (double)_vehicle_local_position.x);
//            printf("y=%8.4f\n", (double)_vehicle_local_position.y);
//            printf("home: x=%8.4f\t", (double)_home_position.x);
//            printf("y=%8.4f\t" , (double)_home_position.y);
//            printf("z=%8.4f\n" , (double)z_home);
//        }


        if (get_a && get_b && get_c) {
            gps_sequence = 0;
            count_time++;
//            ref_a.cos_lat = cos(global_a.lat * pi / 180);
//            ref_a.sin_lat = sin(global_a.lat * pi / 180);
//            ref_a.lon_rad = global_a.lon * pi / 180;
//            ref_a.lat_rad = global_a.lat * pi / 180;
//            wgs_ned(&ref_a, global_b.lat, global_b.lon, &x_b, &y_b);
//            wgs_ned(&ref_a, global_c.lat, global_c.lon, &x_c, &y_c);
//            wgs_ned(&ref_a, _vehicle_global_position.lat, _vehicle_global_position.lon, &x_now, &y_now);

            /* 显示abc点的NED坐标*/
            if (count_time == 5) {
                printf("  a:  x=%6.3f\t", (double)local_a.x);
                printf("y=%6.3f\t", (double)local_a.y);
                printf("z=%6.3f\n", (double)local_a.z);

                printf("  b:  x=%6.3f\t", (double)local_b.x);
                printf("y=%6.3f\t", (double)local_b.y);
                printf("z=%6.3f\n", (double)local_b.z);

                printf("  c:  x=%6.3f\t", (double)local_c.x);
                printf("y=%6.3f\t", (double)local_c.y);
                printf("z=%6.3f\n", (double)local_c.z);

                printf("now:  x=%6.3f\t", (double)local_now.x);
                printf("y=%6.3f\t", (double)local_now.y);
                printf("z=%6.3f\n", (double)local_now.z);

                count_time = 0;
            }
            printf("count_time= %6d\n",count_time);

            sum_square = (double)( (local_a.x - local_now.x) * (local_a.x - local_now.x) +
                                   (local_a.y - local_now.y) * (local_a.y - local_now.y) );
            if (sum_square < 1.0) {
                close_a = true;
                printf("close_a\t");
            }

            sum_square = (double)( (local_b.x - local_now.x) * (local_b.x - local_now.x) +
                                   (local_b.y - local_now.y) * (local_b.y - local_now.y));
            if (sum_square < 1.0) {
                close_b = true;
                printf("close_b\t");
            }

            sum_square = (double)( (local_c.x - local_now.x) * (local_c.x - local_now.x) +
                                   (local_c.y - local_now.y) * (local_c.y - local_now.y));
            if (sum_square < 1.0) {
                close_c = true;
                printf("close_c\t");
            }

            sum_square = (double)( vz * vz * 100);
            if (sum_square < 0.25) {
                is_vz_zero = true;
                printf("vz_zero\n");
            }

            sum_square = (double)(100 * vx * vx + 100 * vy * vy);
            if (sum_square < 0.5) {
                is_vxy_zero = true;
                printf("vxy_zero\n");
            }


            switch (token) {
            case 1:     //A点起飞
                _offboard_sp.ignore_alt_hold = true;
                _offboard_sp.ignore_attitude = true;
                _offboard_sp.ignore_position = false;
                _offboard_sp.ignore_velocity = true;
                _offboard_sp.is_idle_sp = false;
                _offboard_sp.is_land_sp = false;
                _offboard_sp.is_local_frame = true;
                _offboard_sp.is_loiter_sp = false;
                _offboard_sp.is_takeoff_sp = true;
                _offboard_sp.x = local_a.x;
                _offboard_sp.y = local_a.y;
                _offboard_sp.z = local_a.z + set_high;
                _offboard_sp.timestamp = hrt_absolute_time();
                if (local_now.z < (local_a.z + set_high + (float)0.5)) {
                    token = 10;//10悬停5s
                }
                printf("A take off\n");
                break;

            case 2:     //飞向B点
                _offboard_sp.ignore_alt_hold = false;
                _offboard_sp.ignore_position = false;
                _offboard_sp.is_land_sp = false;
                _offboard_sp.is_takeoff_sp = false;
                _offboard_sp.x = local_b.x;
                _offboard_sp.y = local_b.y;
                _offboard_sp.z = local_a.z + set_high;
                if ( close_b && is_vxy_zero) {
                    token = 3;
                }
                printf("TO B\n");
                break;

            case 3:     //B点投放
                count_drop++;
                if (count_drop > 30) {
                    _delivery_signal.is_point_b = true;
                }
                if (count_drop > 50) {
                    count_drop = 0;
                    token = 4;
                }
                printf("Drop B\n");
                break;

            case 4:     //飞向C点
                _offboard_sp.ignore_alt_hold = false;
                _offboard_sp.ignore_position = false;
                _offboard_sp.is_land_sp = false;
                _offboard_sp.is_takeoff_sp = false;
                _offboard_sp.x = local_c.x;
                _offboard_sp.y = local_c.y;
                _offboard_sp.z = local_a.z + set_high;
                if ( close_c && is_vxy_zero) {
                    token = 5;
                }
                printf("TO C\n");
                break;

            case 5:     //C点降落
                _offboard_sp.ignore_alt_hold = true;
                _offboard_sp.ignore_position = false;
                _offboard_sp.is_land_sp = true;
                _offboard_sp.is_takeoff_sp = false;
                _offboard_sp.x = local_c.x;
                _offboard_sp.y = local_c.y;
                _offboard_sp.z = local_c.z + (float)0.3;
                if ((local_now.z > (local_c.z - (float32)2.0)) && is_vz_zero) {
                        token = 6;
                }
                printf("Land C\n");
                printf("token=  %4d\n",token);
                break;

            case 6:     //C点投放
                count_drop++;
                if (count_drop > 30) {
                    _delivery_signal.is_point_c = true;
                }
                if (count_drop > 50) {
                    count_drop = 0;
                    token = 7;
                }
                printf("Drop C\n");
                break;

            case 7:     //C点起飞
                _offboard_sp.ignore_alt_hold = true;
                _offboard_sp.ignore_position = false;
                _offboard_sp.is_land_sp = false;
                _offboard_sp.is_takeoff_sp = true;
                _offboard_sp.x = local_c.x;
                _offboard_sp.y = local_c.y;
                _offboard_sp.z = set_high;
                if (local_now.z < (local_a.z + set_high + (float)0.5)) {
                    token = 11;
                }
                printf("C take off\n");
                break;

            case 8:     //飞向A点
                _offboard_sp.ignore_alt_hold = false;
                _offboard_sp.ignore_position = false;
                _offboard_sp.is_land_sp = false;
                _offboard_sp.is_takeoff_sp = false;
                _offboard_sp.x = local_a.x;
                _offboard_sp.y = local_a.y;
                _offboard_sp.z = local_a.z + set_high;
                if ( close_a && is_vxy_zero) {
                    token = 9;
                }
                printf("To A\n");
                break;

            case 9:     //A点降落
                _offboard_sp.ignore_alt_hold = true;
                _offboard_sp.ignore_position = false;
                _offboard_sp.is_land_sp = true;
                _offboard_sp.is_takeoff_sp = false;
                _offboard_sp.x = local_a.x;
                _offboard_sp.y = local_a.y;
                _offboard_sp.z = local_a.z + (float32)0.2;
                printf("Land A\n");
                break;

            case 10:    //A起飞后悬停5s,下一步是2
                _offboard_sp.ignore_alt_hold = true;
                _offboard_sp.ignore_position = false;
                _offboard_sp.is_land_sp = false;
                _offboard_sp.is_takeoff_sp = false;
                _offboard_sp.x = local_a.x;
                _offboard_sp.y = local_a.y;
                _offboard_sp.z = local_a.z + set_high;
                count_drop++;
                if (count_drop > 5) {
                    count_drop = 0;
                    token = 2;
                }
                printf("Loiter A\n");
                break;

            case 11:    //C起飞后悬停5s,下一步是8
                _offboard_sp.ignore_alt_hold = true;
                _offboard_sp.ignore_position = false;
                _offboard_sp.is_land_sp = false;
                _offboard_sp.is_takeoff_sp = false;
                _offboard_sp.x = local_c.x;
                _offboard_sp.y = local_c.y;
                _offboard_sp.z = local_a.z + set_high;
                count_drop++;
                if (count_drop > 5) {
                    count_drop = 0;
                    token = 8;
                }
                printf("Loiter C\n");
                break;

            default:
                break;
            }
            close_a = false;
            close_b = false;
            close_c = false;
            is_vxy_zero = false;
            is_vz_zero = false;

        }

        _offboard_sp.timestamp = hrt_absolute_time();
        orb_publish(ORB_ID(delivery_signal), delivery_signal_pub, &_delivery_signal);
        orb_publish(ORB_ID(offboard_setpoint), offboard_setpoint_pub, &_offboard_sp);
        usleep(100000);
    }

    warnx("[daemon] exiting.\n");

    thread_running = false;

    return 0;
}
