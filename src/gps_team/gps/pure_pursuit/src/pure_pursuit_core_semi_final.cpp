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
  , left_right()
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
int tf_idx_1 = 1000; // 1180
int tf_idx_2 = 1000; // 1455

int slow_down_tf_idx_1 = 1000;
int slow_down_tf_idx_2 = 1000;

const float tf_coord1[2] = {935574.25, 1915924.125};
const float tf_coord2[2] = {935635.015829, 1915940.86975};

const float slow_down_tf_coord1[2] = {935565.651362, 1915908.85769};
const float slow_down_tf_coord2[2] = {935641.374022, 1915952.16742};


bool index_flag = false;
 
/*************************/



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

  delivery_obs_sub1 = nh_.subscribe("delivery_obs_calc", 1, &PurePursuitNode::callbackFromDeliveryObstacleCalc, this);
  delivery_obs_sub2 = nh_.subscribe("delivery_obs_stop", 1, &PurePursuitNode::callbackFromDeliveryObstacleStop, this);
 
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
  left_right = atoi(argv[2]);

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


    // Traffic Light Index 한번만 초기화 , 신호등 각각 좌표
    if(!index_flag){
      index_flag = true;
      tf_idx_1 = pp_.getPosIndex(tf_coord1[0], tf_coord1[1]);
      tf_idx_2 = pp_.getPosIndex(tf_coord2[0], tf_coord2[1]);

      slow_down_tf_idx_1 = pp_.getPosIndex(slow_down_tf_idx_1[0] , slow_down_tf_idx_1[1]);
      slow_down_tf_idx_2 = pp_.getPosIndex(slow_down_tf_idx_2[0] , slow_down_tf_idx_2[1]);
    }

    ROS_INFO("MODE=%d, MISSION_FLAG=%d", pp_.mode, pp_.mission_flag);


    // MODE 0 - Normal 직진구간
    if(pp_.mode == 0){
      pp_.mission_flag = 0;
      const_lookahead_distance_ = 6;
      //const_velocity_ = 12;
      const_velocity_ = 7;
      final_constant = 1.2;
    }

    // MODE 1 - Normal 커브구간
    if (pp_.mode == 1) {
      pp_.mission_flag = 0;
      const_lookahead_distance_ = 4;
      //const_velocity_ = 10;
      const_velocity_ = 6;
      final_constant = 1.5;
    }

    // MODE 2 : 신호등 (직진구간)
    if (pp_.mode == 2) {
      pp_.mission_flag = 0;
      const_lookahead_distance_ = 5;
      //const_velocity_ = 8;
      const_velocity_ = 7;
      final_constant = 1.2;

      // When traffic lights are RED at slow_down_point -> SLOWNIG DOWN
      if(pp_.reachMissionIdx(slow_down_tf_idx_1) && !pp_.straight_go_flag) {
        while(const_velocity_ > 2){
            const_velocity_ -= 0.1;
            pulishControlMsg(const_velocity_ , 0);
            ROS_INFO_STREAM("*****RED LIGHT SLOWING DOWN*****");
        }
      }

      // When traffic lights are GREEN at slow_down_point -> SPEEDING UP
      else if(pp_.reachMissionIdx(slow_down_tf_idx_1) && pp_.straight_go_flag){
        while(const_velocity_ < 10){
            const_velocity_ += 0.1;
            pulishControlMsg(const_velocity_ , 0);
            ROS_INFO_STREAM("*****GREEN LIGHT SPEEDING UP*****");
        }
      }

      // 첫 신호등 인덱스 : tf_idx_1
      if(pp_.reachMissionIdx(tf_idx_1) && !pp_.straight_go_flag) {
        pulishControlMsg(0,0);
        continue;
      }
    }

    // MODE 3 : 동적장애물 & 신호등 감지
    if(pp_.mode == 3){
        const_lookahead_distance_ = 6;
        final_constant = 1.2;

        if(pp_.mission_flag == 0) const_velocity_ = 7;
        //동적장애물 멀리서 장애물 감지 -> 감속
        if(pp_.is_dynamic_obstacle_detected_long){
            while(pp_.is_dynamic_obstacle_detected_long){
            if (const_velocity_ > 2) {
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

        // When traffic lights are RED at slow_down_point -> SLOWNIG DOWN
        if(pp_.reachMissionIdx(slow_down_tf_idx_2) && !pp_.straight_go_flag) {
          while(const_velocity_ > 1){
              const_velocity_ -= 0.1;
              pulishControlMsg(const_velocity_ , 0);
              ROS_INFO_STREAM("*****RED LIGHT SLOWING DOWN*****");
          }
        }
        
        // When traffic lights are GREEN at slow_down_point -> SPEEDING UP
        else if(pp_.reachMissionIdx(slow_down_tf_idx_2) && pp_.straight_go_flag){
          while(const_velocity_ < 10){
              const_velocity_ += 0.1;
              pulishControlMsg(const_velocity_ , 0);
              ROS_INFO_STREAM("*****GREEN LIGHT SPEEDING UP*****");
          }
        }

        // 두번째 신호등 인덱스
        if(pp_.reachMissionIdx(tf_idx_2) && !pp_.straight_go_flag) {
          ROS_INFO_STREAM("TRAFFIC LIGTHS DETECT");
          pulishControlMsg(0,0);
          continue;
        }
    }


    // MODE 4 : 정적장애물 전 직진
    if (pp_.mode == 4){
      pp_.mission_flag = 0;
      const_lookahead_distance_ = 6;
      const_velocity_ = 7;
      final_constant = 1.2;
    }
   

    //  MODE 5 : 정적장애물 감지되면 avoidance path로 진로변경 후 원래 global path로 복귀 (드럼통)
    if (pp_.mode == 5) {
      std::cout << "LEFT_RIGHT: " << left_right << '\n';
      if (left_right == 0) //오왼
      {
        if (pp_.mission_flag == 0) {
          pp_.setWaypoints(left_path);
          const_lookahead_distance_ = 3;
          const_velocity_ = 7;
          final_constant = 1.2;
          pp_.mission_flag = 1;
        }
        if (pp_.mission_flag >= 1) {
          std::cout << "************************" << std::endl;
          std::cout << "TIME : " << (std::chrono::duration<double>(std::chrono::system_clock::now() - obs_start)).count() << std::endl;
          std::cout << "mission flag :" << pp_.mission_flag << std::endl;
          std::cout << "obstacles_detected : " << pp_.is_static_obstacle_detected_short << std::endl;
        }
        if (pp_.mission_flag == 1 && pp_.is_static_obstacle_detected_short) {
          const_lookahead_distance_ = 4;
          const_velocity_ = 3.1;
          pp_.mission_flag = 2;
          pulishControlMsg(3.1, 28);
          continue;
        }
        else if (pp_.mission_flag == 2 && pp_.is_static_obstacle_detected_short)
        {
          pulishControlMsg(3.2, 28);
          continue;
        }
        else if (pp_.mission_flag == 2 && !pp_.is_static_obstacle_detected_short) {
          const_lookahead_distance_ = 5.5;
          pp_.setWaypoints(right_path);
          //pp_.setWaypoints(global_path);
          pp_.mission_flag = 3;
          ROS_INFO("PAIH SWITCHNG");
          //오류시 삭제요망
          pulishControlMsg(4, -24);
          //continue;
        }
        else if(pp_.mission_flag == 3 && !pp_.is_finish)
        {
          const_lookahead_distance_ = 4;
          const_velocity_ = 7;
          final_constant = 1.5;
        }
        else if(pp_.mission_flag == 3 && pp_.is_finish)
        {
          pp_.setWaypoints(global_path);
          pp_.mission_flag = 4;
          const_lookahead_distance_ = 4;
          const_velocity_ = 7;
          final_constant = 1.5;
          pp_.is_finish = false;
        }
      }
      else if (left_right == 1) //왼오
      {
        if (pp_.mission_flag == 0) {
          pp_.setWaypoints(right_path);
          const_lookahead_distance_ = 3;
          const_velocity_ = 7;
          final_constant = 1.2;
          pp_.mission_flag = 1;
        }
        if (pp_.mission_flag >= 1) {
          std::cout << "************************" << std::endl;
          std::cout << "TIME : " << (std::chrono::duration<double>(std::chrono::system_clock::now() - obs_start)).count() << std::endl;
          std::cout << "mission flag :" << pp_.mission_flag << std::endl;
          std::cout << "obstacles_detected : " << pp_.is_static_obstacle_detected_short << std::endl;
        }
        if (pp_.mission_flag == 1 && pp_.is_static_obstacle_detected_short) {
          const_lookahead_distance_ = 4;
          const_velocity_ = 3.3;
          pp_.mission_flag = 2;
          pulishControlMsg(3.3, -24);
          continue;
        }
        else if (pp_.mission_flag == 2 && pp_.is_static_obstacle_detected_short)
        {
          pulishControlMsg(3.4, -24);
          continue;
        }
        else if (pp_.mission_flag == 2  && !pp_.is_static_obstacle_detected_short) {
          const_lookahead_distance_ = 5.5;
          pp_.setWaypoints(left_path);
          // pp_.setWaypoints(global_path);
          pp_.mission_flag = 3;
          ROS_INFO("PAIH SWITCHNG");
          //오류시 삭제요망
          pulishControlMsg(4, 28);
          //continue;
        }
        else if(pp_.mission_flag == 3 && !pp_.is_finish)
        {
          const_lookahead_distance_ = 4;
          const_velocity_ = 7;
          final_constant = 1.5;
          //continue;
        }
        else if(pp_.mission_flag == 3 && pp_.is_finish)
        {
          pp_.setWaypoints(global_path);
          pp_.mission_flag = 4;
          const_lookahead_distance_ = 4;
          const_velocity_ = 7;
          final_constant = 1.5;
          pp_.is_finish = false;
          //continue;
        }
      }
    }


    // 마지막 waypoint 에 다다랐으면 점차 속도를 줄이기
    if(pp_.is_finish && pp_.mode != 5){
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
    std::ifstream left_path_file(ROS_HOME + "/paths/" + paths[1] + ".txt");
    while(left_path_file >> x >> y >> mode) {
      p.x = x;
      p.y = y;
      left_path.push_back(std::make_pair(p, mode));
      //std::cout << "parking_path : " << parking_path.back().x << ", " << parking_path.back().y << std::endl;
    }

    std::ifstream right_path_file(ROS_HOME + "/paths/" + paths[2] + ".txt");
    while(right_path_file >> x >> y >> mode) {
      p.x = x;
      p.y = y;
      right_path.push_back(std::make_pair(p, mode));
      // std::cout << "avoidance_path : " << avoidance_path.back().x << ", " << parking_path.back().y << std::endl;
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
