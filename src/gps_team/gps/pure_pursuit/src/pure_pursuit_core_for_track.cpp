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

// For kcity
const float pk_coord1[2] = {935534.247324, 1915849.29071};
const float pk_coord2[2] = {935536.127777, 1915852.74891};
const float pk_coord3[2] = {935537.027791, 1915854.43949};
const float pk_coord4[2] = {935539.530479,  1915859.22427};
const float pk_coord5[2] = {935540.465801, 1915860.89238};
const float pk_coord6[2] = {935541.86021, 1915863.43345};

void PurePursuitNode::initForROS()
{
  // ros parameter settings
  private_nh_.param("const_lookahead_distance", const_lookahead_distance_, 4.0);
  private_nh_.param("const_velocity", const_velocity_, 3.0);
  private_nh_.param("final_constant", final_constant, 1.0);

  nh_.param("vehicle_info/wheel_base", wheel_base_, 1.04);

  ROS_HOME = ros::package::getPath("pure_pursuit");

  // setup subscriber
  pose_sub = nh_.subscribe("current_pose", 1, &PurePursuitNode::callbackFromCurrentPose, this);

  //delivery subscriber
  //delivery_sub = nh_.subscribe("delivery", 1, &PurePursuitNode::callbackFromDelivery, this);


  // setup publisher
  drive_msg_pub = nh_.advertise<race::drive_values>("control_value", 1);
  steering_vis_pub = nh_.advertise<geometry_msgs::PoseStamped>("steering_vis", 1);

  // for visualization
  target_point_pub = nh_.advertise<geometry_msgs::PointStamped>("target_point", 1);
  current_point_pub = nh_.advertise<geometry_msgs::PointStamped>("current_point", 1);
}

void PurePursuitNode::run(char** argv) {
  ROS_INFO_STREAM("pure pursuit for track start");
  // temp
  const_lookahead_distance_ = atof(argv[2]);
  const_velocity_ = atof(argv[3]);
  final_constant = atof(argv[4]);
  parking_num = atoi(argv[5]);

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


    // 예선 트랙주행 반복주행코드 트랙주행이 아닐 시, 주석처리
    if(pp_.is_finish && pp_.mode == 0){
      pp_.is_finish = false;
      is_waypoint_set_ = false;
      continue;
    }


    ROS_INFO("MODE=%d, MISSION_FLAG=%d", pp_.mode, pp_.mission_flag);


    // Normal 직진구간
    // 3,5,8,33
    // 35, 37
    // 21,23,28
    pp_.mission_flag = 0;
    const_lookahead_distance_ = 6;

    const_velocity_ = 12;     // 속도가 12 일 때 까지는 어느정도 제어 가능한 상태임 -> 속도를 늘리고 싶다면 197번 째 줄 안에 있는 throttle 제어수식을 속도에 맞추어 주석해제 후 실행 요망 
                              // (만약 속도가 늘어남에 따라서 차가 트랙 바깥쪽으로 기운다면, 220번째 줄에 있는 steering 값에 있는 상수항 조정 현재는 1.2를 곱해놓음 ->  생각보다 숫자 변화에 따라 변화폭이 크므로, 조금씩 늘릴것!!!!)

    //속도가 13 일때 
    // const_velocity_ = 13;   
 
    //속도가 14 일때
    // const_velocity_ = 14;

    //속도가 15 일때
    // const_velocity_ = 15;

    //속도가 16 일때
    // const_velocity_ = 16;

    final_constant = 1.2;


    publishPurePursuitDriveMsg(can_get_curvature, kappa);

    is_pose_set_ = false;
    loop_rate.sleep();
  }
}


void PurePursuitNode::publishPurePursuitDriveMsg(const bool& can_get_curvature, const double& kappa){
  double throttle_;
  if(can_get_curvature == true){
    throttle_ = -50*(pow(kappa , 2)) + const_velocity_;

    //속도가 13 일때
    //throttle_ = -70*(pow(kappa , 2)) + const_velocity_;

    //속도가 14 일때
    // throttle_ = -87.5*(pow(kappa , 2)) + const_velocity_;

    //속도가 15 일때
    // throttle_ = -100*(pow(kappa , 2)) + const_velocity_;

    //속도가 16 일때
    // throttle_ = -112.5*(pow(kappa , 2)) + const_velocity_;

    
    
    const_lookahead_distance_ =  0.0138*(pow(throttle_ , 2)) + 4;

    // 대안 A
    // const_lookahead_distance_ = 0.5 * throttle_;
  }else{
    throttle_ = 0;
  }

  double steering_radian = convertCurvatureToSteeringAngle(wheel_base_, kappa);
  double steering_ = can_get_curvature ? (steering_radian * 180.0 / M_PI) * -1 * final_constant *1.2 : 0;


  std::cout << "steering : " << steering_ << "\tkappa : " << kappa <<std::endl;
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