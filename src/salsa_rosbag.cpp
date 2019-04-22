#include "salsa/salsa_rosbag.h"

using namespace std;
using namespace gnss_utils;

namespace salsa
{

SalsaRosbag::SalsaRosbag(int argc, char** argv)
{
    start_ = 0;
    duration_ = 1e3;
    seen_imu_topic_ = "";
    getArgs(argc, argv);

    loadParams();
    salsa_.init(param_filename_);
    openBag();
    getEndTime();
    truth_log_.open(salsa_.log_prefix_ + "Truth.log");
    imu_log_.open(salsa_.log_prefix_ + "Imu.log");
    imu_count_between_nodes_ = 0;
}

void SalsaRosbag::loadParams()
{
    double acc_stdev, gyro_stdev;
    get_yaml_node("accel_noise_stdev", param_filename_, acc_stdev);
    get_yaml_node("gyro_noise_stdev", param_filename_, gyro_stdev);
    imu_R_.setZero();
    imu_R_.topLeftCorner<3,3>() = acc_stdev * acc_stdev * I_3x3;
    imu_R_.bottomRightCorner<3,3>() = gyro_stdev * gyro_stdev * I_3x3;


    double pos_stdev, att_stdev;
    get_yaml_node("position_noise_stdev", param_filename_, pos_stdev);
    get_yaml_node("attitude_noise_stdev", param_filename_, att_stdev);
    mocap_R_ << pos_stdev * pos_stdev * I_3x3,   Matrix3d::Zero(),
            Matrix3d::Zero(),   att_stdev * att_stdev * I_3x3;

    get_yaml_eigen("q_mocap_to_NED_pos", param_filename_, q_mocap_to_NED_pos_.arr_);
    get_yaml_eigen("q_mocap_to_NED_att", param_filename_, q_mocap_to_NED_att_.arr_);
}

void SalsaRosbag::displayHelp()
{
    cout << "ROS bag parser" <<endl;
    cout << "-h            display this message" << endl;
    cout << "-f <filename> bagfile to parse REQUIRED" << endl;
    cout << "-y <filename> configuration yaml file REQUIRED" << endl;
    cout << "-s <seconds>  start time" << endl;
    cout << "-d <seconds>  duration" << endl;
    cout << "-i <topic>    IMU Topic" << endl;
    cout << "-p <prefix>   Log Prefix" << endl;
    exit(0);
}

void SalsaRosbag::getArgs(int argc, char** argv)
{
    InputParser argparse(argc, argv);

    if (argparse.cmdOptionExists("-h"))
        displayHelp();
    if (!argparse.getCmdOption("-f", bag_filename_))
        displayHelp();
    if (!argparse.getCmdOption("-y", param_filename_))
        displayHelp();
    argparse.getCmdOption("-s", start_);
    argparse.getCmdOption("-d", duration_);
    argparse.getCmdOption("-i", seen_imu_topic_);
    argparse.getCmdOption("-p", log_prefix_);
}

void SalsaRosbag::openBag()
{
    try
    {
        bag_.open(bag_filename_.c_str(), rosbag::bagmode::Read);
        view_ = new rosbag::View(bag_);
    }
    catch(rosbag::BagIOException e)
    {
        ROS_ERROR("unable to load rosbag %s, %s", bag_filename_.c_str(), e.what());
        exit(-1);
    }
}

double SalsaRosbag::getEndTime()
{
    end_ = start_ + duration_;
    end_ = (end_ < view_->getEndTime().toSec() - view_->getBeginTime().toSec())
            ? end_ : view_->getEndTime().toSec() - view_->getBeginTime().toSec();
    cout << "Playing bag from: = " << start_ << "s to: " << end_ << "s" << endl;
    return end_;
}

void SalsaRosbag::parseBag()
{
    ProgressBar prog(view_->size(), 80);
    prog.set_theme_braille();
    bag_start_ = view_->getBeginTime() + ros::Duration(start_);
    bag_end_ = view_->getBeginTime() + ros::Duration(end_);
    int i = 0;
    for(rosbag::MessageInstance const m  : (*view_))
    {
        if (m.getTime() < bag_start_)
            continue;

        if (m.getTime() > bag_end_)
            break;

        prog.print(i++);

        if (m.isType<sensor_msgs::Imu>())
            imuCB(m);
        else if (m.isType<geometry_msgs::PoseStamped>())
            poseCB(m);
        else if (m.isType<nav_msgs::Odometry>())
            odomCB(m);
        else if (m.isType<inertial_sense::GNSSObsVec>())
            obsCB(m);
        else if (m.isType<inertial_sense::GNSSEphemeris>())
            ephCB(m);
    }
    prog.finished();
    cout << endl;
}

void SalsaRosbag::imuCB(const rosbag::MessageInstance& m)
{
    sensor_msgs::ImuConstPtr imu = m.instantiate<sensor_msgs::Imu>();
    double t = (imu->header.stamp - bag_start_).toSec();
    Vector6d z;
    z << imu->linear_acceleration.x,
            imu->linear_acceleration.y,
            imu->linear_acceleration.z,
            imu->angular_velocity.x,
            imu->angular_velocity.y,
            imu->angular_velocity.z;

    if ((z.array() != z.array()).any())
        return;

    imu_log_.log(t);
    imu_log_.logVectors(z);

    imu_count_between_nodes_++;
    salsa_.imuCallback(t, z, imu_R_);
    if (!seen_imu_topic_.empty() && seen_imu_topic_.compare(m.getTopic()))
        ROS_WARN_ONCE("Subscribed to Two IMU messages, use the -i argument to specify IMU topic");

    seen_imu_topic_ = m.getTopic();
}

void SalsaRosbag::obsCB(const rosbag::MessageInstance& m)
{
    inertial_sense::GNSSObsVecConstPtr obsvec = m.instantiate<inertial_sense::GNSSObsVec>();
    salsa::ObsVec z;
    z.reserve(obsvec->obs.size());

    for (const auto& o : obsvec->obs)
    {
        salsa::Obs new_obs;
        new_obs.t = GTime::fromTime(o.time.time, o.time.sec);
        new_obs.sat = o.sat;
        new_obs.rcv = o.rcv;
        new_obs.SNR = o.SNR;
        new_obs.LLI = o.LLI;
        new_obs.code = o.code;
        new_obs.z << o.P, o.D*(Satellite::LAMBDA_L1/0.002), o.L;
        z.push_back(new_obs);
    }
    salsa_.obsCallback(z);
    imu_count_between_nodes_ = 0;
}

void SalsaRosbag::ephCB(const rosbag::MessageInstance &m)
{
    inertial_sense::GNSSEphemerisConstPtr eph = m.instantiate<inertial_sense::GNSSEphemeris>();
    eph_t new_eph;
    new_eph.sat = eph->sat;
    new_eph.iode = eph->iode;
    new_eph.iodc = eph->iodc;
    new_eph.sva = eph->sva;
    new_eph.svh = eph->svh;
    new_eph.week = eph->week;
    new_eph.code = eph->code;
    new_eph.flag = eph->flag;
    new_eph.toe = GTime::fromTime(eph->toe.time, eph->toe.sec);
    new_eph.toc = GTime::fromTime(eph->toc.time, eph->toc.sec);
    new_eph.ttr = GTime::fromTime(eph->ttr.time, eph->ttr.sec);
    new_eph.A = eph->A;
    new_eph.e = eph->e;
    new_eph.i0 = eph->i0;
    new_eph.OMG0 = eph->OMG0;
    new_eph.omg = eph->omg;
    new_eph.M0 = eph->M0;
    new_eph.deln = eph->deln;
    new_eph.OMGd = eph->OMGd;
    new_eph.idot = eph->idot;
    new_eph.crc = eph->crc;
    new_eph.crs = eph->crs;
    new_eph.cuc = eph->cuc;
    new_eph.cus = eph->cus;
    new_eph.cic = eph->cic;
    new_eph.cis = eph->cis;
    new_eph.toes = eph->toes;
    new_eph.fit = eph->fit;
    new_eph.f0 = eph->f0;
    new_eph.f1 = eph->f1;
    new_eph.f2 = eph->f2;
    new_eph.tgd[0] = eph->tgd[0];
    new_eph.tgd[1] = eph->tgd[1];
    new_eph.tgd[2] = eph->tgd[2];
    new_eph.tgd[3] = eph->tgd[3];
    new_eph.Adot = eph->Adot;
    new_eph.ndot = eph->ndot;
    salsa_.ephCallback(GTime::fromUTC(m.getTime().sec, m.getTime().nsec /1e9), new_eph);
}

void SalsaRosbag::poseCB(const rosbag::MessageInstance& m)
{
    geometry_msgs::PoseStampedConstPtr pose = m.instantiate<geometry_msgs::PoseStamped>();
    double t = (m.getTime() - bag_start_).toSec();
    Xformd z;
    z.arr() << pose->pose.position.x,
            pose->pose.position.y,
            pose->pose.position.z,
            pose->pose.orientation.w,
            pose->pose.orientation.x,
            pose->pose.orientation.y,
            pose->pose.orientation.z;

    // The mocap is a North, Up, East (NUE) reference frame, so we have to rotate the quaternion's
    // axis of rotation to NED by 90 deg. roll. Then we rotate that resulting quaternion by -90 deg.
    // in yaw because Leo thinks zero attitude is facing East, instead of North.
    z.t_ = q_mocap_to_NED_pos_.rotp(z.t_);
    z.q_.arr_.segment<3>(1) = q_mocap_to_NED_pos_.rotp(z.q_.arr_.segment<3>(1));
    z.q_ = z.q_ * q_mocap_to_NED_att_;


    if (imu_count_between_nodes_ > 2)
    {
        salsa_.mocapCallback(t, z, mocap_R_);
        imu_count_between_nodes_ = 0;
    }

    Vector3d v = Vector3d::Ones() * NAN;
    Vector6d b = Vector6d::Ones() * NAN;
    Vector2d tau = Vector2d::Ones() * NAN;
    truth_log_.log(t);
    truth_log_.logVectors(z.arr(), v, b, tau);
}

void SalsaRosbag::odomCB(const rosbag::MessageInstance &m)
{
    nav_msgs::OdometryConstPtr odom = m.instantiate<nav_msgs::Odometry>();
    GTime gtime = GTime::fromUTC(odom->header.stamp.sec, odom->header.stamp.nsec/1e9);
    if (salsa_.start_time_.tow_sec < 0)
        return;

    Xformd z;
    z.arr() << odom->pose.pose.position.x,
               odom->pose.pose.position.y,
               odom->pose.pose.position.z,
               odom->pose.pose.orientation.w,
               odom->pose.pose.orientation.x,
               odom->pose.pose.orientation.y,
               odom->pose.pose.orientation.z;
    double t = (m.getTime() - bag_start_).toSec();
    if (imu_count_between_nodes_ > 20)
    {
        salsa_.mocapCallback(t, z, mocap_R_);
        imu_count_between_nodes_ = 0;
    }


    truth_log_.log(
                   (gtime - salsa_.start_time_).toSec(),
//                   (m.getTime() - bag_start_).toSec(),
                   odom->pose.pose.position.x,
                   odom->pose.pose.position.y,
                   odom->pose.pose.position.z,
                   odom->pose.pose.orientation.w,
                   odom->pose.pose.orientation.x,
                   odom->pose.pose.orientation.y,
                   odom->pose.pose.orientation.z,
                   odom->twist.twist.linear.x,
                   odom->twist.twist.linear.y,
                   odom->twist.twist.linear.z,
                   (double)NAN, (double)NAN, (double)NAN,
                   (double)NAN, (double)NAN, (double)NAN,
                   (double)NAN, (double)NAN);
}

}

int main(int argc, char** argv)
{
    salsa::SalsaRosbag thing(argc, argv);
    thing.parseBag();
}
