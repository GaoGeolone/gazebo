/*
 * Copyright (C) 2016 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>
#include <sdf/sdf.hh>
#include <ignition/math/Filter.hh>
#include <gazebo/common/Assert.hh>
#include <gazebo/common/Plugin.hh>
#include <gazebo/msgs/msgs.hh>
#include <gazebo/sensors/sensors.hh>
#include <gazebo/transport/transport.hh>
#include "plugins/RTControlSynchronizationPlugin.hh"

#define MAX_MOTORS 255

using namespace gazebo;

GZ_REGISTER_MODEL_PLUGIN(RTControlSynchronizationPlugin)

// Private data class
class gazebo::RTControlSynchronizationPluginPrivate
{
  ////////////////////////////////////////////////////////////////////////////
  //                                                                        //
  //                                                                        //
  ////////////////////////////////////////////////////////////////////////////

  ////////////////////////////////////////////////////////////////////////////
  //                                                                        //
  //  Controller Synchronization Control                                    //
  //                                                                        //
  ////////////////////////////////////////////////////////////////////////////
  class Command
  {
    public: common::Time timestamp;
  };
  public: Command lastCommand;

  /// \brief enforce delay policy
  public: void EnforceSynchronizationDelay(const common::Time &_curTime,
    int _timeoutMs)
  {
    if (_timeoutMs != 0)
    {
      common::Time curWallTime = common::Time::GetWallTime();
      if (curWallTime >= this->delayWindowStart + this->delayWindowSize)
      {
        this->delayWindowStart = curWallTime;
        this->delayInWindow = common::Time(0.0);
      }

      common::Time delayInStepSum(0.0);
      if (this->delayInWindow < this->delayMaxPerWindow)
      {
        while (delayInStepSum < this->delayMaxPerStep &&
               this->delayInWindow < this->delayMaxPerWindow)
        {
          std::unique_lock<std::mutex> lock(this->mutex);

          double age = _curTime.Double() - this->lastCommand.timestamp.Double();

          // printf("age %f stamp %f\n", age,
          //       this->atlasCommand.header.stamp.toSec()*1000);
          // fflush(stdout);

          // if age is small enough, skip, otherwise, wait finite amount
          // for AtlasCommand messages to catchup.
          if (age <= 0.001 * _timeoutMs)
            break;

          // calculate amount of time to wait based on rules
          // std::chrono::time_point<std::chrono::high_resolution_clock,
          //                         std::chrono::milliseconds>
          std::chrono::system_clock::time_point
            now = std::chrono::system_clock::now();

          long int delayMsInt = floor(1e6*std::min(
              (this->delayMaxPerStep - delayInStepSum).Double(),
              (this->delayMaxPerWindow - this->delayInWindow).Double()));
          std::chrono::microseconds delay(delayMsInt);

          std::chrono::time_point<std::chrono::system_clock> timeout =
            now + delay;

          // common::Time tmp(boost::detail::get_timespec(timeout));
          // printf("timeout %f wall %f min(%f, %f)\n",
          //     tmp.Double(), delayTime.Double(),
          //     (this->delayMaxPerStep - delayInStepSum).Double(),
          //     (this->delayMaxPerWindow - this->delayInWindow).Double());

          // convert now to common::Time
          // common::Time delayTime(std::chrono::get_timespec(now));
          gzerr << "now: "
                << std::chrono::time_point_cast<std::chrono::microseconds>(now).time_since_epoch().count() << "\n";
          timespec tv;
          /*
          // std::chrono::seconds const sec =
          std::chrono::duration<double, std::ratio<1>> sec =
            std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - now);
          tv.tv_sec  = static_cast<int>(sec.count());
          tv.tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(
            now - sec).count();

          tv.tv_sec  = static_cast<int>(sec.count());
          tv.tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(
          */
          common::Time delayTime(tv);

          if (this->delayCondition.wait_until(lock, timeout) !=
              std::cv_status::timeout)
          {
            delayTime = common::Time::GetWallTime() - delayTime;
            if ((this->delayInWindow >= this->delayMaxPerWindow) ||
                (delayInStepSum >= this->delayMaxPerStep))
              gzwarn << "controller synchronization timedout: "
                     << "delay budget exhausted.\n";
            else
              gzwarn << "AtlasPlugin controller synchronization timedout: "
                     << "message lost or controller stopped.\n";

            // printf("sim %f timed out with %f delayed %f\n",
            //       _curTime.Double()*1000,
            //       this->atlasCommand.header.stamp.toSec()*1000,
            //       delayTime.Double()*1000);
            // fflush(stdout);
          }
          else
          {
            delayTime = common::Time::GetWallTime() - delayTime;
            if (delayTime >= this->delayMaxPerStep)
              gzwarn << "AtlasPlugin controller synchronization timeout: "
                     << "waited full duration, but timed_wait returned true.\n";
            // printf("nsim %f otified with %f delayed %f\n",
            //       _curTime.Double()*1000,
            //       this->atlasCommand.header.stamp.toSec()*1000,
            //       delayTime.Double()*1000);
            // fflush(stdout);
          }

          // printf(" sum before (%f, %f) ",
          //   delayInStepSum.Double(),
          //   this->delayInWindow.Double());

          delayInStepSum += delayTime;
          this->delayInWindow += delayTime;

          // printf(" after (%f, %f)\n",
          //   delayInStepSum.Double(),
          //   this->delayInWindow.Double());
        }
        // printf(" out of while (%f < %f) (%f < %f)\n",
        //   delayInStepSum.Double(), this->delayMaxPerStep.Double(),
        //   this->delayInWindow.Double(), this->delayMaxPerWindow.Double());
      }
      /*
      this->delayStatistics.delay_in_step = delayInStepSum.Double();
      this->delayStatistics.delay_in_window = this->delayInWindow.Double();
      this->delayStatistics.delay_window_remain =
        ((this->delayWindowStart + this->delayWindowSize) -
         curWallTime).Double();
      this->pubDelayStatisticsQueue->push(
        this->delayStatistics, this->pubDelayStatistics);
      */
    }
  }

  public: void PublishConstrollerStatistics(const common::Time &_curTime)
  {
    /*
    /// publish controller statistics diagnostics, damages, etc.
    if (this->controllerStatsConnectCount > 0)
    {
      if ((_curTime - this->lastControllerStatisticsTime).Double() >=
        1.0/this->statsUpdateRate)
      {
        atlas_msgs::ControllerStatistics msg;
        msg.header.stamp = ros::Time(_curTime.sec, _curTime.nsec);
        msg.command_age = this->atlasCommandAge;
        msg.command_age_mean = this->atlasCommandAgeMean;
        msg.command_age_variance = this->atlasCommandAgeVariance /
          (this->atlasCommandAgeBuffer.size() - 1);
        msg.command_age_window_size = this->atlasCommandAgeBufferDuration;

        this->pubControllerStatisticsQueue->push(msg,
          this->pubControllerStatistics);
        this->lastControllerStatisticsTime = _curTime;
      }
    }
    */
  }

  /// \brief Special topic for advancing simulation by a ROS topic.
  public: gazebo::transport::SubscriberPtr subTic;

  /// \brief Condition variable for tic-ing simulation step.
  public: std::condition_variable delayCondition;

  /// \brief a non-moving window is used, every delayWindowSize-seconds
  /// the user is allotted delayMaxPerWindow seconds of delay budget.
  public: common::Time delayWindowSize;

  /// \brief Marks the start of a non-moving delay window.
  public: common::Time delayWindowStart;

  /// \brief Within each window, simulation will wait at
  /// most a total of delayMaxPerWindow seconds.
  public: common::Time delayMaxPerWindow;

  /// \brief Within each simulation step, simulation will wait at
  /// most delayMaxPerStep seconds to receive information from controller.
  public: common::Time delayMaxPerStep;

  /// \brief Within each window, simulation will wait at
  /// most a total of delayMaxPerWindow seconds.
  public: common::Time delayInWindow;

  ////////////////////////////////////////////////////////////////////////////
  //                                                                        //
  //  controller staticstics                                                //
  //                                                                        //
  ////////////////////////////////////////////////////////////////////////////

  /// \brief Publish controller synchronization delay information.
  /*
  public: gazebo::transport::Publisher pubDelayStatistics;
  public: PubQueue<atlas_msgs::SynchronizationStatistics>::Ptr
    pubDelayStatisticsQueue;
  public: atlas_msgs::SynchronizationStatistics delayStatistics;
  public: common::Time lastControllerStatisticsTime;
  */

  /// \brief Keep track of number of controller stats connections
  public: int controllerStatsConnectCount;

  /// \brief Mutex to protect controllerStatsConnectCount.
  public: std::mutex statsConnectionMutex;

  ////////////////////////////////////////////////////////////////////////////
  //                                                                        //
  //  connect to gazebo                                                     //
  //                                                                        //
  ////////////////////////////////////////////////////////////////////////////
  /// \brief Pointer to the update event connection.
  public: event::ConnectionPtr updateConnection;

  /// \brief Pointer to the model;
  public: physics::ModelPtr model;

  /// \brief keep track of controller update sim-time.
  public: gazebo::common::Time lastUpdateTime;

  /// \brief Controller update mutex.
  public: std::mutex mutex;
};

////////////////////////////////////////////////////////////////////////////////
RTControlSynchronizationPlugin::RTControlSynchronizationPlugin()
  : dataPtr(new RTControlSynchronizationPluginPrivate)
{
  // default control synchronization delay settings
  // to trigger synchronization delay, set
  // timeoutMs to non-zero
  this->timeoutMs = 1;

  // synchronization variables
  this->dataPtr->delayWindowSize = common::Time(5.0);
  this->dataPtr->delayMaxPerWindow = common::Time(0.25);
  this->dataPtr->delayMaxPerStep = common::Time(0.025);
  this->dataPtr->delayWindowStart = common::Time(0.0);
  this->dataPtr->delayInWindow = common::Time(0.0);
  this->dataPtr->controllerStatsConnectCount = 0;
}

/////////////////////////////////////////////////
RTControlSynchronizationPlugin::~RTControlSynchronizationPlugin()
{
}

/////////////////////////////////////////////////
void RTControlSynchronizationPlugin::Load(physics::ModelPtr _model, sdf::ElementPtr _sdf)
{
  GZ_ASSERT(_model, "_model pointer is null");
  GZ_ASSERT(_sdf, "_sdf pointer is null");

  this->dataPtr->model = _model;

  // Connect to the update event.
  // This event is broadcast by gzserver on every simulation step.
  this->dataPtr->updateConnection = event::Events::ConnectWorldUpdateBegin(
      boost::bind(&RTControlSynchronizationPlugin::OnUpdate, this, _1));
}

/////////////////////////////////////////////////
void RTControlSynchronizationPlugin::OnUpdate(const common::UpdateInfo &_info)
{
  std::lock_guard<std::mutex> lock(this->dataPtr->mutex);

  gazebo::common::Time curTime = this->dataPtr->model->GetWorld()->GetSimTime();
  double dt = (curTime - this->dataPtr->lastUpdateTime).Double();
  this->dataPtr->lastUpdateTime = curTime;

  // Update the control surfaces and publish the new state.
  if (dt > 0.0)
  {
    this->RobotStateOut();
    this->dataPtr->EnforceSynchronizationDelay(curTime, this->timeoutMs);
    this->RobotCommandIn();
    this->ApplyRobotCommandToSim(dt);
    this->dataPtr->PublishConstrollerStatistics(curTime);
  }
}

/////////////////////////////////////////////////
void RTControlSynchronizationPlugin::RobotStateOut()
{
}

/////////////////////////////////////////////////
void RTControlSynchronizationPlugin::ApplyRobotCommandToSim(const double _dt)
{
}

/////////////////////////////////////////////////
void RTControlSynchronizationPlugin::RobotCommandIn()
{
  // receive robot command from the outside world and tick simulation
  std::lock_guard<std::mutex> lock(this->dataPtr->mutex);

  // do whatever needed to be done here to receive the robot command
  // from the controller, usually copy to a local buffer
  this->ReceiveRobotCommand();

  // tic simulation
  this->dataPtr->delayCondition.notify_all();
}

/////////////////////////////////////////////////
void RTControlSynchronizationPlugin::ReceiveRobotCommand()
{
  // to be overloaded by user
}

/////////////////////////////////////////////////
void RTControlSynchronizationPlugin::SendRobotState()
{
  // to be overloaded by user
}

/////////////////////////////////////////////////
void RTControlSynchronizationPlugin::Tic()
{
  std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
  this->dataPtr->delayCondition.notify_all();
}
