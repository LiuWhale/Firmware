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
#include <uORB/topics/offboard_setpoint.h>
#include <uORB/topics/home_position.h>
#include <uORB/topics/radar_pos.h>

static bool thread_should_exit = false;     /**< daemon exit flag */
static bool thread_running = false;     /**< daemon status flag */
static int get_gps_data_task;             /**< Handle of daemon task / thread */

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

    usage("unrecognized command");
    return 1;

}

int get_data_thread_main(int argc, char *argv[])
{

    warnx("[daemon] starting\n");

    thread_running = true;

    bool close_1 = false;
//    bool close_b = false;
//    bool close_c = false;
//    bool is_vxy_zero = false;

//    int count = 0;
    int count_time = 0;
//    int count_drop = 0;
    int token = 1;
    double sum_square = 0.0;

    int rc_channels_sub = orb_subscribe(ORB_ID(rc_channels));
    int home_position_sub = orb_subscribe(ORB_ID(home_position));
    int radar_pos_sub = orb_subscribe(ORB_ID(radar_pos));

    struct rc_channels_s _rc_channels = {};
    struct delivery_signal_s _delivery_signal = {};
    struct home_position_s _home_position = {};
    struct offboard_setpoint_s _offboard_sp = {};
    struct radar_pos_s _radar_pos = {};

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
    orb_advert_t radar_pos_pub = orb_advertise(ORB_ID(radar_pos), &_radar_pos);

    orb_publish(ORB_ID(offboard_setpoint), offboard_setpoint_pub, &_offboard_sp);
    orb_publish(ORB_ID(delivery_signal), delivery_signal_pub, &_delivery_signal);
    orb_publish(ORB_ID(radar_pos), radar_pos_pub, &_radar_pos);//get target point data

    while (!thread_should_exit) {

        bool updated_rcch;
        bool updated_vp_local;
        bool updated_home;

        orb_check(rc_channels_sub, &updated_rcch);
        orb_check(home_position_sub, &updated_home);
	orb_check(radar_pos_sub, &updated_vp_local);
	

        if (updated_rcch) {
            orb_copy(ORB_ID(rc_channels), rc_channels_sub, &_rc_channels);
            printf("channel 1 = %8.4f\n", (double)_rc_channels.channels[0]);
            printf("channel 2 = %8.4f\n", (double)_rc_channels.channels[1]);
            printf("channel 3 = %8.4f\n", (double)_rc_channels.channels[2]);
            printf("channel 4 = %8.4f\n", (double)_rc_channels.channels[3]);
            printf("channel 5 = %8.4f\n", (double)_rc_channels.channels[4]);
            printf("channel 6 = %8.4f\n", (double)_rc_channels.channels[7]);
        }



        if (updated_home) {
            orb_copy(ORB_ID(home_position), home_position_sub, &_home_position);
        }

        if (updated_vp_local) {
		orb_copy(ORB_ID(radar_pos), radar_pos_sub, &_radar_pos);

		    //显示abc点的NED坐标
		printf("  1:  x=%6.3f\t", (double)_radar_pos.point_datax_1);
		printf("y=%6.3f\t", (double)_radar_pos.point_datay_1);

		printf("  2:  x=%6.3f\t", (double)_radar_pos.point_datax_2);
		printf("y=%6.3f\t", (double)_radar_pos.point_datay_2);

		printf("  3:  x=%6.3f\t", (double)_radar_pos.point_datax_3);
		printf("y=%6.3f\t", (double)_radar_pos.point_datay_3);



		printf("now:  x=%6.3f\t", (double)_radar_pos.local_datax);
		printf("y=%6.3f\t", (double)_radar_pos.local_datay);
		count_time = 0;

		printf("count_time= %6d\n",count_time);

		sum_square = (double)( (_radar_pos.point_datax_1 - _radar_pos.local_datax) * (_radar_pos.point_datax_1 - _radar_pos.local_datax) +
				   (_radar_pos.point_datay_1 - _radar_pos.local_datay) * (_radar_pos.point_datay_1 - _radar_pos.local_datay) );
		if (sum_square < 1.0) {
		close_1 = true;
		printf("close_1\t");
		}

		sum_square = (double)( (_radar_pos.point_datax_2 - _radar_pos.local_datax) * (_radar_pos.point_datax_2 - _radar_pos.local_datax) +
				   (_radar_pos.point_datay_2 - _radar_pos.local_datay) * (_radar_pos.point_datay_2 - _radar_pos.local_datay) );
		if (sum_square < 1.0) {
		//close_b = true;
		printf("close_2\t");
		}

		sum_square = (double)( (_radar_pos.point_datax_3 - _radar_pos.local_datax) * (_radar_pos.point_datax_3 - _radar_pos.local_datax) +
				   (_radar_pos.point_datay_3 - _radar_pos.local_datay) * (_radar_pos.point_datay_3 - _radar_pos.local_datay) );
		if (sum_square < 1.0) {
		//close_c = true;
		printf("close_3\t");
		}


		switch (token) {
		case 1:
		        _offboard_sp.ignore_alt_hold = false;
		        _offboard_sp.ignore_position = false;
		        _offboard_sp.is_land_sp = false;
		        _offboard_sp.is_takeoff_sp = false;
		        _offboard_sp.x = _radar_pos.point_datax_1;
		        _offboard_sp.y = _radar_pos.point_datay_1;
		        _offboard_sp.z = set_high;
		        if (close_1) {
		            token = 12;
		        }
			break;

		default:
			break;
		}
		close_1 = false;
		//close_b = false;
		//close_c = false;
		//is_vxy_zero = false;

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
