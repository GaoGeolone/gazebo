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

#include <memory>
#include <mutex>


#include "gazebo/msgs/msgs.hh"
#include "gazebo/transport/TransportIface.hh"

#include "gazebo/gui/plot/TopicCurveHandler.hh"
#include "gazebo/gui/plot/IntrospectionCurveHandler.hh"
#include "gazebo/gui/plot/PlotCurve.hh"
#include "gazebo/gui/plot/PlotWindow.hh"
#include "gazebo/gui/plot/IncrementalPlot.hh"
#include "gazebo/gui/plot/PlotManager.hh"

using namespace gazebo;
using namespace gui;

namespace gazebo
{
  namespace gui
  {
    /// \brief Private data for the PlotManager class
    class PlotManagerPrivate
    {
      /// \brief Mutex to protect plot manager updates.
      public: std::mutex mutex;

      /// \brief Node for communications.
      public: transport::NodePtr node;

      /// \brief Subscriber to the world control topic
      public: transport::SubscriberPtr worldControlSub;

      /// \brief A map of topic names to topic data handers.
      //public: std::map<std::string, TopicDataHandler *> topicDataHandlers;

      /// \brief Handler for updating topic curves
      public: TopicCurveHandler topicCurve;

      /// \brief Handler for updating introspection curves
      public: IntrospectionCurveHandler introspectionCurve;

      /// \brief A list of plot windows.
      public: std::vector<PlotWindow *> windows;
    };
  }
}

/////////////////////////////////////////////////
PlotManager::PlotManager()
  : dataPtr(new PlotManagerPrivate())
{
  this->dataPtr->node = transport::NodePtr(new transport::Node());
  this->dataPtr->node->Init();

  // check for reset events and restart plots when time is reset
  this->dataPtr->worldControlSub =
      this->dataPtr->node->Subscribe("~/world_control",
      &PlotManager::OnWorldControl, this);

}

/////////////////////////////////////////////////
PlotManager::~PlotManager()
{
}

/////////////////////////////////////////////////
void PlotManager::OnWorldControl(ConstWorldControlPtr &_data)
{
  if (_data->has_reset())
  {
    if ((_data->reset().has_all() && _data->reset().all())  ||
        (_data->reset().has_time_only() && _data->reset().time_only()))
    {
      std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
      for (auto &w : this->dataPtr->windows)
        w->Restart();
    }
  }
}

/*
/////////////////////////////////////////////////
void PlotManager::AddTopicCurve(const std::string &_uri,
    PlotCurveWeakPtr _curve)
{
  std::lock_guard<std::mutex> lock(this->dataPtr->mutex);

  // parse the uri to get topic
  std::string topic;

  // parse the uri to get param;
  std::string param;

  auto it = this->dataPtr->topicDataHandlers.find(topic);
  if (it == this->dataPtr->topicDataHandlers.end())
  {
    TopicDataHandler *handler = new TopicDataHandler();
    handler->SetTopic(topic);
    handler->AddCurve(param, _curve);
    this->dataPtr->topicDataHandlers[topic] = handler;
  }
  else
  {
    TopicDataHandler *handler = it->second;
    handler->AddCurve(param, _curve);
  }
}

/////////////////////////////////////////////////
void PlotManager::RemoveTopicCurve(PlotCurveWeakPtr _curve)
{
  if (_curve.expired())
    return;

  // find and remove the curve
  std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
  for (auto it = this->dataPtr->topicDataHandlers.begin();
      it != this->dataPtr->topicDataHandlers.end(); ++it)
  {
    TopicDataHandler *handler = it->second;

    if (!handler)
      continue;

    if (handler->HasCurve(_curve))
    {
      handler->RemoveCurve(_curve);
      if (handler->Count() == 0u)
      {
        delete it->second;
        this->dataPtr->topicDataHandlers.erase(it);
      }

      break;
    }
  }
}*/



/*/////////////////////////////////////////////////
void PlotManager::AddCurve(const std::string &_name, PlotCurveWeakPtr _curve)
{
  std::cerr << " add curve " << std::endl;
  std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
  auto c = _curve.lock();
  if (!c)
    return;
  std::cerr << " add curve after lock" << std::endl;

  auto it = this->dataPtr->curves.find(_name);
  if (it == this->dataPtr->curves.end())
  {
    // create entry in map
    CurveVariableSet curveSet;
    curveSet.insert(_curve);
    this->dataPtr->curves[_name] = curveSet;

    this->AddItemToFilter(_name);

  }
  else
  {
    auto cIt = it->second.find(_curve);
    if (cIt == it->second.end())
    {
      it->second.insert(_curve);
    }
  }
  std::cerr << " done add curve" << std::endl;
}

/////////////////////////////////////////////////
void PlotManager::RemoveCurve(PlotCurveWeakPtr _curve)
{
  std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
  auto c = _curve.lock();
  if (!c)
    return;

  // find and remove the curve
  for (auto it = this->dataPtr->curves.begin();
      it != this->dataPtr->curves.end(); ++it)
  {
    auto cIt = it->second.find(_curve);
    if (cIt != it->second.end())
    {
      it->second.erase(cIt);
      if (it->second.empty())
      {
        // remove item from introspection filter
        this->RemoveItemFromFilter(it->first);

        // erase from map
        this->dataPtr->curves.erase(it);
      }
      return;
    }
  }
}*/

/////////////////////////////////////////////////
void PlotManager::AddIntrospectionCurve(const std::string &_uri,
    PlotCurveWeakPtr _curve)
{
  this->dataPtr->introspectionCurve.AddCurve(_uri, _curve);
}

/////////////////////////////////////////////////
void PlotManager::RemoveIntrospectionCurve(PlotCurveWeakPtr _curve)
{
  this->dataPtr->introspectionCurve.RemoveCurve(_curve);
}

/////////////////////////////////////////////////
void PlotManager::AddTopicCurve(const std::string &_uri,
    PlotCurveWeakPtr _curve)
{
  this->dataPtr->topicCurve.AddCurve(_uri, _curve);
}

/////////////////////////////////////////////////
void PlotManager::RemoveTopicCurve(PlotCurveWeakPtr _curve)
{
  this->dataPtr->topicCurve.RemoveCurve( _curve);
}

/////////////////////////////////////////////////
void PlotManager::AddWindow(PlotWindow *_window)
{
  std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
  this->dataPtr->windows.push_back(_window);
}

/////////////////////////////////////////////////
void PlotManager::RemoveWindow(PlotWindow *_window)
{
  std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
  for (auto it = this->dataPtr->windows.begin();
      it != this->dataPtr->windows.end(); ++it)
  {
    if ((*it) == _window)
    {
      this->dataPtr->windows.erase(it);
      return;
    }
  }
}