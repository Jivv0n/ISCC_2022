#include <vector>
#include <cmath>
#include <pure_pursuit_core.h>
#include <fstream>
#include <cstdlib>
#include <unistd.h>
#include <time.h>
#include <chrono>
#include <algorithm>

#include <tf/transform_broadcaster.h>


namespace waypoint_follower
{
// Constructor
PurePursuitNode::PurePursuitNode()
  : private_nh_("~")
  , pp_()
  , LOOP_RATE_(30)
  , is_waypoint_set_(false)
  , is_pose_set_(false)
  , const_lookahead_distance_(4.0)
  , const_velocity_(3.0)
  , final_constant(1.5)
  , parking_num(1)
{
  initForROS();
}

// Destructor
PurePursuitNode::~PurePursuitNode() {}

// obstacle global variable
float tmp_yaw_rate = 0.0;

bool left_detected = false;
bool left_avoid = false;
bool right_detected = false;
bool right_avoid = false;

float target_dist = 3.0;
float min_dist = 1.0;

int obs_cnt = 0;
std::chrono::system_clock::time_point obs_start;

float steering_memory = 0;

/* traffic Index manager */
// 1,2,3,4,7 직진
// 5(비보호 좌회전), 6(좌회전)
int tf_idx_1 = 1000;
int tf_idx_2 = 1000;
int tf_idx_3 = 1000;
int tf_idx_4 = 1000;
int tf_idx_5 = 1000;
int tf_idx_6 = 1000;
int tf_idx_7 = 1000;

int tf_idx_8 = 1000;
int tf_idx_9 = 1000;


bool index_flag = false;
bool is_parked = false;

// 0914 new stop_line path
const float tf_coord1[2] = {935573.8125, 1915923.125};
const float tf_coord2[2] = {935598.43036, 1915969.19758};
const float tf_coord3[2] = {935650.071767194, 1916093.28736585};
const float tf_coord4[2] = {935656.5625, 1916202.0};
const float tf_coord5[2] = {935649.125, 1916336.375};
const float tf_coord6[2] = {935581.563637, 1916255.3587};
const float tf_coord7[2] = {935642.8125, 1916140.375};

// 무시할지결정해야
const float tf_coord8[2] = {935611.629488, 1916009.71394};
const float tf_coord9[2] = {935592.698823, 1915969.01965};
 
/*************************/


/* Parking manager */
int start_parking_idx = 0;
int end_parking_idx = 0;
int end_parking_backward_idx = 0;
int end_parking_full_steer_backward_idx = 0;

// For kcity (kcity 주차 좌표)
const float pk_coord1[2] = {935534.247324, 1915849.29071};
const float pk_coord2[2] = {935536.127777, 1915852.74891};
const float pk_coord3[2] = {935537.027791, 1915854.43949};
const float pk_coord4[2] = {935539.530479, 1915859.22427};
const float pk_coord5[2] = {935540.465801, 1915860.89238};
const float pk_coord6[2] = {935541.86021, 1915863.43345};

// For School Test (학교 주차 좌표)
// const float pk_coord1[2] = {955565.3630135682, 1956933.4946035568};
// const float pk_coord2[2] = {955564.96476695, 1956933.8079647133};
// const float pk_coord3[2] = {955564.6498305532, 1956934.0536339642};
// const float pk_coord4[2] = {955564.018495136, 1956934.5500655076};
// const float pk_coord5[2] = {955563.8305732157, 1956934.6975132197};
// const float pk_coord6[2] = {955563.4789975542, 1956934.971366751};
/*************************/

// Delivery Distance
double min_a_dist = 9999999;
double min_b_dist = 9999999;

// Delivery var
double delivery_x_dist;
double delivery_angle;

// max index of pp_.a_cnt array
int a_max_index = -1;
int b_max_index = -1;
// calc max index flag
bool a_cnt_flag = false;
bool b_cnt_flag = false;

int b_idx_cnt = 0;

std::vector<int> passed_index;


void PurePursuitNode::initForROS()
{
  // ros parameter settings
  private_nh_.param("const_lookahead_distance", const_lookahead_distance_, 4.0);
  private_nh_.param("const_velocity", const_velocity_, 3.0);
  private_nh_.param("final_constant", final_constant, 1.0);

  nh_.param("vehicle_info/wheel_base", wheel_base_, 1.04);

  ROS_HOME = ros::package::getPath("pure_pursuit");

  // setup subscriber
  pose_sub = nh_.subscribe("current_pose", 1,
    &PurePursuitNode::callbackFromCurrentPose, this);

  // for main control
  static_obstacle_short_sub = nh_.subscribe("/static_obs_flag_short", 1, &PurePursuitNode::callbackFromStaticObstacleShort, this);
  static_obstacle_long_sub = nh_.subscribe("/static_obs_flag_long", 1, &PurePursuitNode::callbackFromStaticObstacleLong, this);
  dynamic_obstacle_short_sub = nh_.subscribe("/dynamic_obs_flag_short", 1, &PurePursuitNode::callbackFromDynamicObstacleShort, this);
  dynamic_obstacle_long_sub = nh_.subscribe("/dynamic_obs_flag_long", 1, &PurePursuitNode::callbackFromDynamicObstacleLong, this);


  traffic_light_sub = nh_.subscribe("darknet_ros/bounding_boxes",1, &PurePursuitNode::callbackFromTrafficLight, this);

  delivery_obs_sub1 = nh_.subscribe("delivery_information", 1, &PurePursuitNode::callbackFromDeliveryObstacleStop, this);
 
  // obstacle_sub = nh_.subscribe("{lane_topic_name}", 1,
  //   &PurePursuitNode::callbackFromLane, this);

  //delivery subscriber
  delivery_sub = nh_.subscribe("delivery", 1, &PurePursuitNode::callbackFromDelivery, this);


  // setup publisher
  drive_msg_pub = nh_.advertise<race::drive_values>("control_value", 1);
  steering_vis_pub = nh_.advertise<geometry_msgs::PoseStamped>("steering_vis", 1);

  // for visualization
  target_point_pub = nh_.advertise<geometry_msgs::PointStamped>("target_point", 1);
  current_point_pub = nh_.advertise<geometry_msgs::PointStamped>("current_point", 1);
}

void PurePursuitNode::run(char** argv) {
  ROS_INFO_STREAM("pure pursuit2 start");
  // temp
  parking_num = atoi(argv[2]);

  ros::Rate loop_rate(LOOP_RATE_);
  while (ros::ok()) {
    ros::spinOnce();

    if (!is_waypoint_set_) {
      setPath(argv);
      pp_.setWaypoints(global_path);
    }

    if (!is_pose_set_) {
      loop_rate.sleep();
      continue;
    }

    pp_.setLookaheadDistance(computeLookaheadDistance());

    double kappa = 0;
    bool can_get_curvature = pp_.canGetCurvature(&kappa);

    // target point visualization
    publishTargetPointVisualizationMsg();
    publishCurrentPointVisualizationMsg();

    if(pp_.mode != 7){ 

      //동적장애물 멀리서 장애물 감지 -> 감속
      if(pp_.is_dynamic_obstacle_detected_long){
        while(pp_.is_dynamic_obstacle_detected_long){
          if (const_velocity_ > 3) {
            const_velocity_ -= 0.1;
          }
          pulishControlMsg(const_velocity_ , steering_memory);
          ROS_INFO_STREAM("LONG OBSTACLE DETECT");
          ros::spinOnce();
          loop_rate.sleep();
        }
      }

      // 동적장애물 멈춰야하는 거리
      if(pp_.is_dynamic_obstacle_detected_short){
        while(pp_.is_dynamic_obstacle_detected_short){
          pulishControlMsg(0 , steering_memory);
          ROS_INFO_STREAM("OBSTACLE DETECT");
          // 1초
          //usleep(1000000);
          ros::spinOnce();
          loop_rate.sleep();
        }
        const_velocity_ = 3;
      }
    }

    // Traffic Light Index 한번만 초기화 , 신호등 각각 좌표
    if(!index_flag){
      index_flag = true;
      tf_idx_1 = pp_.getPosIndex(tf_coord1[0], tf_coord1[1]);
      tf_idx_2 = pp_.getPosIndex(tf_coord2[0], tf_coord2[1]);
      tf_idx_3 = pp_.getPosIndex(tf_coord3[0], tf_coord3[1]);
      tf_idx_4 = pp_.getPosIndex(tf_coord4[0], tf_coord4[1], 1); // 혹시나 배달구간 겹칠 수 있기때문에..
      tf_idx_5 = pp_.getPosIndex(tf_coord5[0], tf_coord5[1]);
      tf_idx_6 = pp_.getPosIndex(tf_coord6[0], tf_coord6[1]);
      tf_idx_7 = pp_.getPosIndex(tf_coord7[0], tf_coord7[1]);

      tf_idx_8 = pp_.getPosIndex(tf_coord8[0], tf_coord8[1]);
      tf_idx_9 = pp_.getPosIndex(tf_coord9[0], tf_coord9[1]);

      
    }

    ROS_INFO("MODE=%d, MISSION_FLAG=%d", pp_.mode, pp_.mission_flag);


    // Normal 직진구간
    // 3,5,8,33
    // 35, 37
    // 21,23,28

    // 신호등, 미션 전 직진
    if(pp_.mode == 0 || pp_.mode == 2 || pp_.mode == 4 || pp_.mode == 6 || pp_.mode == 27 || pp_.mode == 32 || pp_.mode == 34 || pp_.mode == 36){
      pp_.mission_flag = 0;
      const_lookahead_distance_ = 6;
      const_velocity_ = 11;
      // const_velocity_ = 7;
      final_constant = 1.2;
    }

    // MODE 22 : 교차로 차선변경 전 직진
    if(pp_.mode == 22){
      pp_.mission_flag = 0;
      const_lookahead_distance_ = 6;
      const_velocity_ = 10;
      // const_velocity_ = 7;
      final_constant = 1.2;
    }

    // 마지막 신호등 전 직진
    if(pp_.mode == 34 || pp_.mode == 36){
      pp_.mission_flag = 0;
      const_lookahead_distance_ = 6;
      const_velocity_ = 11;
      // const_velocity_ = 7;
      final_constant = 1.2;
    }

    // Normal 직진
    if(pp_.mode == 11 || pp_.mode == 13 || pp_.mode == 15 || pp_.mode == 17 || pp_.mode == 25 || pp_.mode == 30){
      pp_.mission_flag = 0;
      const_lookahead_distance_ = 6;
      const_velocity_ = 13;
      // const_velocity_ = 4;
      final_constant = 1.2;
    }

    // Normal 커브
    if (pp_.mode == 12 || pp_.mode == 14 || pp_.mode == 16 || pp_.mode == 18 || pp_.mode == 24 || pp_.mode == 26 || pp_.mode == 29 || pp_.mode == 31) {
      pp_.mission_flag = 0;
      const_lookahead_distance_ = 4;
      const_velocity_ = 11;
      // const_velocity_ = 7;
      final_constant = 1.5;
    }

    // MODE 1 - 주차
    // 주차 구간
    if (pp_.mode == 1) {
      pp_.is_finish = false;

      if (pp_.mission_flag == 3 || pp_.mission_flag == 0) {
        const_lookahead_distance_ = 6;
        const_velocity_ = 6;
      }
      if (!is_parked)
      {
        // first
        if (parking_num == 1){
          start_parking_idx = pp_.getPosIndex(pk_coord1[0], pk_coord1[1]);
          end_parking_idx = 120;
          end_parking_backward_idx = 100;
          end_parking_full_steer_backward_idx = 75;
        }

        // second
        if (parking_num == 2){
          start_parking_idx = pp_.getPosIndex(pk_coord2[0], pk_coord2[1]);
          end_parking_idx = 87;
          end_parking_backward_idx = 55;
          end_parking_full_steer_backward_idx = 25;
        }

        // third
        if (parking_num == 3){
          start_parking_idx = pp_.getPosIndex(pk_coord3[0], pk_coord3[1]);
          end_parking_idx = 145;
          end_parking_backward_idx = 117; // 120
          end_parking_full_steer_backward_idx = 90;
        }  

        // forth
        if (parking_num == 4){
          start_parking_idx = pp_.getPosIndex(pk_coord4[0], pk_coord4[1]);
          end_parking_idx = 88;
          end_parking_backward_idx = 55;
          end_parking_full_steer_backward_idx = 23;
        }

        // fifth
        if (parking_num == 5){
          start_parking_idx = pp_.getPosIndex(pk_coord5[0], pk_coord5[1]);
          end_parking_idx = 80;
          end_parking_backward_idx = 50;
          end_parking_full_steer_backward_idx = 23;
        }

        //sixth
        if (parking_num == 6){
          start_parking_idx = pp_.getPosIndex(pk_coord6[0], pk_coord6[1]);
          end_parking_idx = 75;
          end_parking_backward_idx = 55;
          end_parking_full_steer_backward_idx = 25;
        }

        int backward_speed = -7;

        if (pp_.mission_flag == 0 && pp_.next_waypoint_number_ >= start_parking_idx){
          pp_.setWaypoints(parking_path);
          const_lookahead_distance_ = 3;
          const_velocity_ = 3;  // 3
          pp_.mission_flag = 1;
        }

        // 주차 끝
        if(pp_.mission_flag == 1 && pp_.reachMissionIdx(end_parking_idx)){
          /////////////////////////////////////////////////////////////////////////////////
          // 5초 멈춤
          for (int i = 0; i < 110; i++)
          {
            pulishControlMsg(0, 0);
            // 0.1초
            usleep(100000);
          }

          /////////////////////////////////////////////////////////////////////////////////
          // 특정 지점까지는 그냥 후진
          while (!pp_.reachMissionIdx(end_parking_backward_idx)) {
            pulishControlMsg(backward_speed, 0);
            ros::spinOnce();
          }
          // 그 다음 지점까지는 풀조향 후진
          while (!pp_.reachMissionIdx(end_parking_full_steer_backward_idx)) {
            pulishControlMsg(backward_speed, 30);
            ros::spinOnce();
          }
          pp_.mission_flag = 2;
        }

        // 주차 빠져나오고 다시 global path로
        if (pp_.mission_flag == 2) {
          for (int i = 0; i < 30; i++) {
            pulishControlMsg(0, 0);
            // 0.1초
            usleep(100000);
          }
          ROS_INFO("PATH SWITCHING");
          pp_.setWaypoints(global_path);
          pp_.mission_flag = 3;
        }

        if (pp_.mission_flag == 3)
        {
          const_lookahead_distance_ = 6;
          const_velocity_ = 10;
          final_constant = 1.2;
          is_parked = true;
        }
      }
    }

    //  MODE 7 : 정적장애물 감지되면 avoidance path로 진로변경 후 원래 global path로 복귀
    if (pp_.mode == 7) {
      if (pp_.mission_flag == 0) {
        const_lookahead_distance_ = 6;
        const_velocity_ = 8;
      }
      if (pp_.mission_flag >= 1) {
        std::cout << "************************" << std::endl;
        std::cout << "TIME : " << (std::chrono::duration<double>(std::chrono::system_clock::now() - obs_start)).count() << std::endl;
        std::cout << "mission flag :" << pp_.mission_flag << std::endl;
        std::cout << "obstacles_detected : " << pp_.is_static_obstacle_detected_short << std::endl;
      }
      if (pp_.mission_flag == 0 && pp_.is_static_obstacle_detected_short) {
        const_lookahead_distance_ = 4;
        const_velocity_ = 5;
        pp_.mission_flag = 1;
        // pulishControlMsg(4, 24);

        // 더 완만하게 돌게하기위함
        pulishControlMsg(4, 20);
        continue;
      }
      
      else if (pp_.mission_flag == 1 && !pp_.is_static_obstacle_detected_short) {
        pp_.setWaypoints(avoidance_path);
        pp_.mission_flag = 2;
        ROS_INFO("PAITH SWITCHNG");
        // pulishControlMsg(4, -28);

        // 더 완만하게 돌게하기위함
        pulishControlMsg(4, -24);
        continue;
      }
      else if (pp_.mission_flag == 1 && pp_.is_static_obstacle_detected_short) {
        //pp_.mission_flag = 2;
        //pulishControlMsg(4, 24);

        // 더 완만하게 돌게하기위함
        pulishControlMsg(4, 20);
        continue;
      }
      // else if (pp_.mission_flag == 2 && !pp_.is_static_obstacle_detected_long)
      // {
      //   const_lookahead_distance_ = 4;
      //   const_velocity_ = 3;
      //   continue;
      // }
      else if (pp_.mission_flag == 2 && pp_.is_static_obstacle_detected_short) {
        pp_.mission_flag = 3;
        // pulishControlMsg(4, -28);

        // 더 완만하게 돌게하기위함
        pulishControlMsg(4, -24);
        continue;
      }
      else if(pp_.mission_flag == 3 && pp_.is_static_obstacle_detected_short)
      {
        // pulishControlMsg(4, -28);

        // 더 완만하게 돌게하기위함
        pulishControlMsg(4, -24);
        continue;
      }
      else if (pp_.mission_flag == 3 && !pp_.is_static_obstacle_detected_short) {
        pp_.setWaypoints(global_path);
        pp_.mission_flag = 4;
        const_lookahead_distance_ = 4;
        const_velocity_ = 8;
        final_constant = 1.5;
      }
    }

    // MODE 3,5,8,33 : 신호등 (직진구간)
    if (pp_.mode == 3 || pp_.mode == 5 || pp_.mode == 8 || pp_.mode == 33) {
      pp_.mission_flag = 0;
      const_lookahead_distance_ = 5;
      const_velocity_ = 7;
      final_constant = 1.2;

      // 1,2,3,7번 직진신호등 멈춤
      if((pp_.reachMissionIdx(tf_idx_1) || pp_.reachMissionIdx(tf_idx_2) || pp_.reachMissionIdx(tf_idx_3) || pp_.reachMissionIdx(tf_idx_7)) && !pp_.straight_go_flag) {
        pulishControlMsg(0,0);
        continue;
      }
    }

    

    // MODE 21, 23 : 신호등 & 커브
    if (pp_.mode == 21 || pp_.mode == 23){
      pp_.mission_flag = 0;
      const_lookahead_distance_ = 5;
      const_velocity_ = 7;
      final_constant = 1.5;

      if((pp_.reachMissionIdx(tf_idx_4) || pp_.reachMissionIdx(tf_idx_5)) && !pp_.straight_go_flag) {
        pulishControlMsg(0,0);
        continue;
      }
    }

    // MODE 28 : 신호등 (비보호 좌회전 신호)
    if (pp_.mode == 28) {
      pp_.mission_flag = 0;
      const_lookahead_distance_ = 5;
      const_velocity_ = 8;
      final_constant = 1.2;


      // 5,6번 좌회전 신호등 멈춤
      if((pp_.reachMissionIdx(tf_idx_6)) && !pp_.left_go_flag) {
        pulishControlMsg(0,0);
        continue;
      }
    }

    // MODE 35,37 : 신호등
    if (pp_.mode == 35 || pp_.mode == 37) {
      pp_.mission_flag = 0;
      const_lookahead_distance_ = 5;
      const_velocity_ = 7;
      final_constant = 1.2;

      if((pp_.reachMissionIdx(tf_idx_8) || pp_.reachMissionIdx(tf_idx_9)) && !pp_.straight_go_flag) {
        pulishControlMsg(0,0);
        continue;
      }
    }


    // MODE 9 : 배달 A 전 진입구간
    if (pp_.mode == 9){
      pp_.mission_flag = 0;
      const_lookahead_distance_ = 4;
      const_velocity_ = 7.5;
      //const_velocity_ = 4;
      final_constant = 1.8;
    }


    // MODE 10 : 배달 PICK A
    if (pp_.mode == 10) {
      const_lookahead_distance_ = 6;
      // const_velocity_ = 4;
      const_velocity_ = 5;
      final_constant = 1.2;

      if(pp_.mission_flag == 0 && pp_.is_delivery_obs_stop_detected == 1) { // msg.x <= 0
        // ROS_INFO("DELIVERY OBSTACLE DETECT!!!");
        pp_.mission_flag = 1;        
        for (int i = 0; i < 55; i++)
        {
          pulishControlMsg(0, 0);
          // 0.1초
          usleep(100000);
          std::cout << "############# PICK A STOP ###############" << '\n';
         
        }
        continue;
      }
      if(pp_.mission_flag == 1){
        // ROS_INFO("a_cnt_flag&&&&&&&&&&&&&&&&&&&& : %d", a_cnt_flag);
        const_velocity_ = 6; // if not calculated a_max_index
      }
    }

    // 배달 A 구간 끝나고 바로 A_INDEX 계산함
    if (pp_.mode == 11 && !a_cnt_flag) {
      // if not calculated a_max_index
      // Calc max_index
      a_max_index = max_element(pp_.a_cnt.begin(), pp_.a_cnt.end()) - pp_.a_cnt.begin();
      ROS_INFO("A MAX INDEX : %d", a_max_index);

      // Lock the a_cnt_flag
      a_cnt_flag = true;
    }

    // MODE 19 : 배달 B 전 진입구간
    if (pp_.mode == 19){
      pp_.mission_flag = 0;
      pp_.is_delivery_obs_stop_detected = 0;
      const_lookahead_distance_ = 4;
      const_velocity_ = 6;
      //const_velocity_ = 4;
      final_constant = 1.5;
    }

    // MODE 20 : 배달 PICK B
    if (pp_.mode == 20) {
      const_lookahead_distance_ = 6;
      // const_velocity_ = 4;
      const_velocity_ = 5;
      final_constant = 1.2;
      
      ROS_INFO("MISSION_FLAG=(%d) A_INDEX(%d)  B_INDEX(%d)", pp_.mission_flag, a_max_index, b_max_index);
      ROS_INFO("B1=%d, B2=%d, B3=%d", pp_.b_cnt[0], pp_.b_cnt[1], pp_.b_cnt[2]);
      
      // case 2) vision_distance + gps 로직
      if(pp_.mission_flag == 0){
        pp_.mission_flag = 1;
      }
      else if(pp_.mission_flag == 22){
        pp_.mission_flag = 2;
      }
      else if(pp_.mission_flag == 33){
        pp_.mission_flag = 3;
      }

      
      if((pp_.mission_flag == 1 || pp_.mission_flag == 2 || pp_.mission_flag == 3) && pp_.is_delivery_obs_stop_detected == 1){
        // pp_.is_delivery_obs_stop_detected = 0;
         b_max_index = max_element(pp_.b_cnt.begin(), pp_.b_cnt.end()) - pp_.b_cnt.begin();
       
        ROS_INFO("MISSION_FLAG_FINAL=(%d) A_INDEX_FINAL(%d)  B_INDEX_FINAL(%d)", pp_.mission_flag, a_max_index, b_max_index);
        ROS_INFO("HOW MANY B COUNT=%d",  pp_.b_cnt[b_max_index] );
        
        if (a_max_index == b_max_index) {
          ROS_INFO("@@@@@@@@@@@@@@@ DELIVERY COMPLETE @@@@@@@@@@@@@@");
          // std::cout << pp_.b_cnt << std::endl;
          // Max flag on
          for (int i = 0; i < 55; i++)
          {
            pulishControlMsg(0, 0);
            usleep(100000);  // 0.1초
          }
          pp_.mission_flag = 100;
        } 
        else {
          pp_.b_cnt = {0, 0, 0};
          if (pp_.mission_flag == 1) { pp_.mission_flag = 22; }
          else if (pp_.mission_flag == 2) { pp_.mission_flag = 33; }
        }
      }

      if(pp_.mission_flag == 100) {
        const_lookahead_distance_ = 4;
        const_velocity_ = 6;
        final_constant = 1.4;
      }
    }

    // MODE 38 - 직선 구간 (부스터)
    if(pp_.mode == 38){
      pp_.mission_flag = 0;
      const_lookahead_distance_ = 6;
      //const_velocity_ = 18;
      const_velocity_ = 7;
      final_constant = 1.2;
    }

    // 마지막 waypoint 에 다다랐으면 점차 속도를 줄이기
    if(pp_.is_finish && pp_.mode == 38){
      while(const_velocity_ > 0)
      {
        const_velocity_ -= 1;
        pulishControlMsg(const_velocity_,0);
      }
      ROS_INFO_STREAM("Finish Pure Pursuit");
      //마지막 waypoint라면 코드를 종료하기 위함 안될시 삭제요망
      ros::shutdown(); 
      continue;
    }

    publishPurePursuitDriveMsg(can_get_curvature, kappa);

    is_pose_set_ = false;
    loop_rate.sleep();
  }
}


void PurePursuitNode::publishPurePursuitDriveMsg(const bool& can_get_curvature, const double& kappa) {
  double throttle_ = can_get_curvature ? const_velocity_ : 0;
  double steering_radian = convertCurvatureToSteeringAngle(wheel_base_, kappa);
  double steering_ = can_get_curvature ? (steering_radian * 180.0 / M_PI) * -1 * final_constant: 0;


  // std::cout << "steering : " << steering_ << "\tkappa : " << kappa <<std::endl;
  pulishControlMsg(throttle_, steering_);

  // for steering visualization
  publishSteeringVisualizationMsg(steering_radian);
}

double PurePursuitNode::computeLookaheadDistance() const {
  if (true) {
    return const_lookahead_distance_;
  }
}

void PurePursuitNode::pulishControlMsg(double throttle, double steering) const
{
  race::drive_values drive_msg;
  drive_msg.throttle = throttle;
  drive_msg.steering = steering;
  drive_msg_pub.publish(drive_msg);

  steering_memory = drive_msg.steering;
}


void PurePursuitNode::callbackFromCurrentPose(const geometry_msgs::PoseStampedConstPtr& msg) {
  pp_.setCurrentPose(msg);
  is_pose_set_ = true;
}

void PurePursuitNode::setPath(char** argv) {
  std::vector<std::string> paths;
  path_split(argv[1], paths, ",");
  std::ifstream global_path_file(ROS_HOME + "/paths/" + paths[0] + ".txt");
  //std::cout << ROS_HOME + "/paths/" + argv[1] << std::endl;

  // path.txt
  // <x, y, mode>
  geometry_msgs::Point p;
  double x, y;
  int mode;

  while(global_path_file >> x >> y >> mode) {
    p.x = x;
    p.y = y;
    //pp_.mode = mode;

    global_path.push_back(std::make_pair(p, mode));
    //std::cout << "global_path : " << global_path.back().x << ", " << global_path.back().y << std::endl;
  }
  if (paths.size() == 3) {
    std::ifstream parking_path_file(ROS_HOME + "/paths/" + paths[1] + ".txt");
    while(parking_path_file >> x >> y >> mode) {
      p.x = x;
      p.y = y;
      parking_path.push_back(std::make_pair(p, mode));
      //std::cout << "parking_path : " << parking_path.back().x << ", " << parking_path.back().y << std::endl;
    }

    std::ifstream avoidance_path_file(ROS_HOME + "/paths/" + paths[2] + ".txt");
    while(avoidance_path_file >> x >> y >> mode) {
      p.x = x;
      p.y = y;
      avoidance_path.push_back(std::make_pair(p, mode));
      // std::cout << "avoidance_path : " << avoidance_path.back().x << ", " << parking_path.back().y << std::endl;
    }
  }
  else if (paths.size() == 2) {
    std::ifstream parking_path_file(ROS_HOME + "/paths/" + paths[1] + ".txt");
    while(parking_path_file >> x >> y >> mode) {
      p.x = x;
      p.y = y;
      parking_path.push_back(std::make_pair(p, mode));
      // std::cout << "parking_path : " << parking_path.back().x << ", " << parking_path.back().y << std::endl;
    }
  }

  is_waypoint_set_ = true;
}

void PurePursuitNode::publishTargetPointVisualizationMsg () {
  geometry_msgs::PointStamped target_point_msg;
  target_point_msg.header.frame_id = "/base_link";
  target_point_msg.header.stamp = ros::Time::now();
  target_point_msg.point = pp_.getPoseOfNextTarget();
  target_point_pub.publish(target_point_msg);
}

void PurePursuitNode::publishCurrentPointVisualizationMsg () {
  geometry_msgs::PointStamped current_point_msg;
  current_point_msg.header.frame_id = "/base_link";
  current_point_msg.header.stamp = ros::Time::now();
  current_point_msg.point = pp_.getCurrentPose();
  current_point_pub.publish(current_point_msg);
}

void PurePursuitNode::publishSteeringVisualizationMsg (const double& steering_radian) const {
  double yaw = atan2(2.0 * (pp_.current_pose_.orientation.w * pp_.current_pose_.orientation.z + pp_.current_pose_.orientation.x * pp_.current_pose_.orientation.y), 1.0 - 2.0 * (pp_.current_pose_.orientation.y * pp_.current_pose_.orientation.y + pp_.current_pose_.orientation.z * pp_.current_pose_.orientation.z));

  double steering_vis = yaw + steering_radian;
  geometry_msgs::Quaternion _quat = tf::createQuaternionMsgFromYaw(steering_vis);
  geometry_msgs::PoseStamped pose;
  pose.header.stamp = ros::Time::now();
  pose.header.frame_id = "/base_link";
  pose.pose.position = pp_.current_pose_.position;
  pose.pose.orientation = _quat;
  steering_vis_pub.publish(pose);
}

void PurePursuitNode::callbackFromDynamicObstacleShort(const std_msgs::Bool& msg) {
  pp_.is_dynamic_obstacle_detected_short = msg.data;
}

void PurePursuitNode::callbackFromDynamicObstacleLong(const std_msgs::Bool& msg) {
  pp_.is_dynamic_obstacle_detected_long = msg.data;
}

void PurePursuitNode::callbackFromStaticObstacleShort(const std_msgs::Bool& msg) {
  pp_.is_static_obstacle_detected_short = msg.data;
}

void PurePursuitNode::callbackFromStaticObstacleLong(const std_msgs::Bool& msg) {
  pp_.is_static_obstacle_detected_long = msg.data;
}


// for delivery obstacle (stop) - 멈추는 로직
void PurePursuitNode::callbackFromDeliveryObstacleStop(const lidar_team_erp42::Delivery& msg) {
  if ((msg.x != 0 &&  msg.angle != 0) && (pp_.mode == 10 || pp_.mode == 20)) {
    // msg.angle >=95 수정
    if (msg.x > -0.05 && msg.x < 0.16) {
      pp_.is_delivery_obs_stop_detected++;
      std::cout << "STOP DETECTED ??????" << pp_.is_delivery_obs_stop_detected << '\n';
    }
    else {
      pp_.is_delivery_obs_stop_detected = 0;
    }
  }
}


/*************************************************************************************************************/

void PurePursuitNode::callbackFromDelivery(const vision_distance::DeliveryArray& msg){
  std::vector<vision_distance::Delivery> deliverySign = msg.visions;

  // B Area
  if (pp_.mode == 20 && (pp_.mission_flag == 1 || pp_.mission_flag == 2 || pp_.mission_flag == 3)){
    sort(deliverySign.begin(), deliverySign.end(), compare2);
    
    if(deliverySign.size() > 0){
      if(deliverySign[0].flag < 4){
        pp_.b_cnt[deliverySign[0].flag-1] += 1;
        std::cout << "deliverySign: " << deliverySign[0].flag-1 << '\n';
        std::cout << "b_cnt: " << pp_.b_cnt[deliverySign[0].flag-1] << '\n';
      }
    }
  }

  // A Area
  if (pp_.mode == 10 && pp_.mission_flag == 0){
    sort(deliverySign.begin(), deliverySign.end(), compare2);
    
    if(deliverySign.size() > 0){
      if(deliverySign[0].flag >= 4){
        pp_.a_cnt[deliverySign[0].flag-4] += 1;
        std::cout << "deliverySign: " << deliverySign[0].flag-4 << '\n';
        std::cout << "a_cnt: " << pp_.a_cnt[deliverySign[0].flag-4] << '\n';
      }
    }
  }
}


void PurePursuitNode::callbackFromTrafficLight(const darknet_ros_msgs::BoundingBoxes& msg) {
  // std::vector<darknet_ros_msgs::BoundingBox> traffic_lights = msg.bounding_boxes;
  // std::sort(traffic_lights.begin(), traffic_lights.end(), compare);

  std::vector<darknet_ros_msgs::BoundingBox> yoloObjects = msg.bounding_boxes;
  std::vector<darknet_ros_msgs::BoundingBox> deliveryObjectsA, deliveryObjectsB;

  // 신호등 객체만 따로 검출함 (원근법 알고리즘 적용위함)
  std::vector<darknet_ros_msgs::BoundingBox> traffic_lights;
  for(int i=0; i<yoloObjects.size(); i++){
    if(yoloObjects[i].Class == "3 red" || yoloObjects[i].Class == "3 yellow" || yoloObjects[i].Class == "3 green" || yoloObjects[i].Class == "3 left"
      || yoloObjects[i].Class == "4 red" || yoloObjects[i].Class == "4 yellow" || yoloObjects[i].Class == "4 green"
      || yoloObjects[i].Class == "4 redleft" || yoloObjects[i].Class == "4 greenleft"  || yoloObjects[i].Class == "4 redyellow"){

        traffic_lights.push_back(yoloObjects[i]);
      }

    // if(pp_.mode == 10){
    //   if(yoloObjects[i].Class == "A1" || yoloObjects[i].Class == "A2" || yoloObjects[i].Class == "A3")
    //     deliveryObjectsA.push_back(yoloObjects[i]);
    // }

    // if(pp_.mode == 20){
    //   if(yoloObjects[i].Class == "B1" || yoloObjects[i].Class == "B2" || yoloObjects[i].Class == "B3")
    //     deliveryObjectsB.push_back(yoloObjects[i]);
    // }

    // if(pp_.mode == 20 && (pp_.mission_flag == 0 || pp_.mission_flag == 22 || pp_.mission_flag == 33)){
    //   if(yoloObjects[i].Class == "B1") pp_.b_cnt[0] += 1;
    //   else if(yoloObjects[i].Class == "B2") pp_.b_cnt[1] += 1;
    //   else if(yoloObjects[i].Class == "B3") pp_.b_cnt[2] += 1;
    // }

    
  }

  std::sort(traffic_lights.begin(), traffic_lights.end(), compare);
  // std::sort(deliveryObjectsA.begin(), deliveryObjectsA.end(), compare);
  // std::sort(deliveryObjectsB.begin(), deliveryObjectsB.end(), compare);

  // if(pp_.mode == 10 && deliveryObjectsA.size() > 0 && pp_.mission_flag == 0){
  //   if(deliveryObjectsA[0].Class == "A1") pp_.a_cnt[0] += 1;
  //   else if(deliveryObjectsA[0].Class == "A2") pp_.a_cnt[1] += 1;
  //   else if(deliveryObjectsA[0].Class == "A3") pp_.a_cnt[2] += 1;
  // }

  // if(pp_.mode == 20 && deliveryObjectsB.size() > 0 && (pp_.mission_flag == 1 || pp_.mission_flag == 2 || pp_.mission_flag == 3)){
  //   auto it1 = find(passed_index.begin(), passed_index.end(), 0);
  //   auto it2 = find(passed_index.begin(), passed_index.end(), 1);
  //   auto it3 = find(passed_index.begin(), passed_index.end(), 2);

  //   // 지나간 index는 무시
  //   if(deliveryObjectsB[0].Class == "B1" && it1 == passed_index.end()) pp_.b_cnt[0] += 1;
  //   else if(deliveryObjectsB[0].Class == "B2" && it2 == passed_index.end()) pp_.b_cnt[1] += 1;
  //   else if(deliveryObjectsB[0].Class == "B3" && it3 == passed_index.end()) pp_.b_cnt[2] += 1;
  // }

 int index = 0;

 
  if(traffic_lights.size() > 1){

      int first_traffic = (traffic_lights[0].xmax - traffic_lights[0].xmin) * (traffic_lights[0].ymax - traffic_lights[0].ymin);
      int second_traffic = (traffic_lights[1].xmax - traffic_lights[1].xmin) * (traffic_lights[1].ymax - traffic_lights[1].ymin);

      if(first_traffic * 0.6 < second_traffic) {
        if (traffic_lights[0].Class == "3 left" || traffic_lights[0].Class == "4 redleft" || traffic_lights[0].Class == "4 greenleft" ||
          traffic_lights[1].Class == "3 left" || traffic_lights[1].Class == "4 redleft" || traffic_lights[1].Class == "4 greenleft") {
            pp_.left_go_flag = true;
        }
        else {
          pp_.left_go_flag = false;
        }

        if (pp_.left_go_flag) {
          std::cout << "left go" << std::endl;
        }
        return;
      }
    }




  if(traffic_lights.size() > 0){

     // debug
    // if (pp_.mode == 3 || pp_.mode == 5 || pp_.mode == 8 || pp_.mode == 21 || pp_.mode == 33){
    //   ROS_INFO("TRAFFIC_LIGHT_SIZE : %d", traffic_lights.size());
    //   ROS_INFO("TRAFFIC : %s", traffic_lights[0].Class);
    // }

    if (traffic_lights[index].Class == "3 red" || traffic_lights[index].Class == "3 yellow" || traffic_lights[index].Class == "4 red" ||
      traffic_lights[index].Class == "4 yellow" || traffic_lights[index].Class == "4 redyellow")
    {
      pp_.straight_go_flag = false;
      pp_.left_go_flag = false;
    }
    else if (traffic_lights[index].Class == "3 green" || traffic_lights[index].Class == "4 green")
    {
      pp_.straight_go_flag = true;
      pp_.left_go_flag = false;
    }
    else if (traffic_lights[index].Class == "3 left" || traffic_lights[index].Class == "4 redleft")
    {
      pp_.straight_go_flag = false;
      pp_.left_go_flag = true;
    }
    else if (traffic_lights[index].Class == "4 greenleft")
    {
      pp_.straight_go_flag = true;
      pp_.left_go_flag = true;
    }
  }


}


double convertCurvatureToSteeringAngle(const double& wheel_base, const double& kappa) {
  return atan(wheel_base * kappa);
}

void path_split(const std::string& str, std::vector<std::string>& cont,
		const std::string& delim)
{
    size_t prev = 0, pos = 0;
    do
    {
        pos = str.find(delim, prev);
        if (pos == std::string::npos) pos = str.length();
        std::string token = str.substr(prev, pos-prev);
        if (!token.empty()) cont.push_back(token);
        prev = pos + delim.length();
    }
    while (pos < str.length() && prev < str.length());
}

bool compare(darknet_ros_msgs::BoundingBox a, darknet_ros_msgs::BoundingBox b) {
  int a_area = (a.ymax - a.ymin) * (a.xmax - a.xmin);
  int b_area = (b.ymax - b.ymin) * (b.xmax - b.xmin);

  return a_area > b_area ? true : false;
}


bool compare2(vision_distance::Delivery a, vision_distance::Delivery b) {
  
  return a.dist_y < b.dist_y ? true : false;
}

}  // namespace waypoint_follower