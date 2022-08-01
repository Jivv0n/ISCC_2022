// Generated by gencpp from file jinho/PurePursuit.msg
// DO NOT EDIT!


#ifndef JINHO_MESSAGE_PUREPURSUIT_H
#define JINHO_MESSAGE_PUREPURSUIT_H


#include <string>
#include <vector>
#include <map>

#include <ros/types.h>
#include <ros/serialization.h>
#include <ros/builtin_message_traits.h>
#include <ros/message_operations.h>


namespace jinho
{
template <class ContainerAllocator>
struct PurePursuit_
{
  typedef PurePursuit_<ContainerAllocator> Type;

  PurePursuit_()
    : throttle(0)
    , steering(0.0)  {
    }
  PurePursuit_(const ContainerAllocator& _alloc)
    : throttle(0)
    , steering(0.0)  {
  (void)_alloc;
    }



   typedef int16_t _throttle_type;
  _throttle_type throttle;

   typedef double _steering_type;
  _steering_type steering;





  typedef boost::shared_ptr< ::jinho::PurePursuit_<ContainerAllocator> > Ptr;
  typedef boost::shared_ptr< ::jinho::PurePursuit_<ContainerAllocator> const> ConstPtr;

}; // struct PurePursuit_

typedef ::jinho::PurePursuit_<std::allocator<void> > PurePursuit;

typedef boost::shared_ptr< ::jinho::PurePursuit > PurePursuitPtr;
typedef boost::shared_ptr< ::jinho::PurePursuit const> PurePursuitConstPtr;

// constants requiring out of line definition



template<typename ContainerAllocator>
std::ostream& operator<<(std::ostream& s, const ::jinho::PurePursuit_<ContainerAllocator> & v)
{
ros::message_operations::Printer< ::jinho::PurePursuit_<ContainerAllocator> >::stream(s, "", v);
return s;
}


template<typename ContainerAllocator1, typename ContainerAllocator2>
bool operator==(const ::jinho::PurePursuit_<ContainerAllocator1> & lhs, const ::jinho::PurePursuit_<ContainerAllocator2> & rhs)
{
  return lhs.throttle == rhs.throttle &&
    lhs.steering == rhs.steering;
}

template<typename ContainerAllocator1, typename ContainerAllocator2>
bool operator!=(const ::jinho::PurePursuit_<ContainerAllocator1> & lhs, const ::jinho::PurePursuit_<ContainerAllocator2> & rhs)
{
  return !(lhs == rhs);
}


} // namespace jinho

namespace ros
{
namespace message_traits
{





template <class ContainerAllocator>
struct IsFixedSize< ::jinho::PurePursuit_<ContainerAllocator> >
  : TrueType
  { };

template <class ContainerAllocator>
struct IsFixedSize< ::jinho::PurePursuit_<ContainerAllocator> const>
  : TrueType
  { };

template <class ContainerAllocator>
struct IsMessage< ::jinho::PurePursuit_<ContainerAllocator> >
  : TrueType
  { };

template <class ContainerAllocator>
struct IsMessage< ::jinho::PurePursuit_<ContainerAllocator> const>
  : TrueType
  { };

template <class ContainerAllocator>
struct HasHeader< ::jinho::PurePursuit_<ContainerAllocator> >
  : FalseType
  { };

template <class ContainerAllocator>
struct HasHeader< ::jinho::PurePursuit_<ContainerAllocator> const>
  : FalseType
  { };


template<class ContainerAllocator>
struct MD5Sum< ::jinho::PurePursuit_<ContainerAllocator> >
{
  static const char* value()
  {
    return "ef2152ed667962c416322fe394052479";
  }

  static const char* value(const ::jinho::PurePursuit_<ContainerAllocator>&) { return value(); }
  static const uint64_t static_value1 = 0xef2152ed667962c4ULL;
  static const uint64_t static_value2 = 0x16322fe394052479ULL;
};

template<class ContainerAllocator>
struct DataType< ::jinho::PurePursuit_<ContainerAllocator> >
{
  static const char* value()
  {
    return "jinho/PurePursuit";
  }

  static const char* value(const ::jinho::PurePursuit_<ContainerAllocator>&) { return value(); }
};

template<class ContainerAllocator>
struct Definition< ::jinho::PurePursuit_<ContainerAllocator> >
{
  static const char* value()
  {
    return "int16 throttle\n"
"float64 steering\n"
;
  }

  static const char* value(const ::jinho::PurePursuit_<ContainerAllocator>&) { return value(); }
};

} // namespace message_traits
} // namespace ros

namespace ros
{
namespace serialization
{

  template<class ContainerAllocator> struct Serializer< ::jinho::PurePursuit_<ContainerAllocator> >
  {
    template<typename Stream, typename T> inline static void allInOne(Stream& stream, T m)
    {
      stream.next(m.throttle);
      stream.next(m.steering);
    }

    ROS_DECLARE_ALLINONE_SERIALIZER
  }; // struct PurePursuit_

} // namespace serialization
} // namespace ros

namespace ros
{
namespace message_operations
{

template<class ContainerAllocator>
struct Printer< ::jinho::PurePursuit_<ContainerAllocator> >
{
  template<typename Stream> static void stream(Stream& s, const std::string& indent, const ::jinho::PurePursuit_<ContainerAllocator>& v)
  {
    s << indent << "throttle: ";
    Printer<int16_t>::stream(s, indent + "  ", v.throttle);
    s << indent << "steering: ";
    Printer<double>::stream(s, indent + "  ", v.steering);
  }
};

} // namespace message_operations
} // namespace ros

#endif // JINHO_MESSAGE_PUREPURSUIT_H
