#ifndef OPENTERA_WEBRTC_NATIVE_CLIENT_SOURCES_ROS_VIDEO_SOURCE_H
#define OPENTERA_WEBRTC_NATIVE_CLIENT_SOURCES_ROS_VIDEO_SOURCE_H

#include <media/base/adapted_video_track_source.h>
#include <rtc_base/ref_counted_object.h>
#include <sensor_msgs/Image.h>

namespace introlab
{
    class RosVideoSource : public rtc::RefCountedObject<rtc::AdaptedVideoTrackSource>
    {
    public:
        void imageCallback(const sensor_msgs::ImageConstPtr& msg);

        bool is_screencast() const override;
        bool remote() const override;
        absl::optional<bool> needs_denoising() const override;
        webrtc::MediaSourceInterface::SourceState state() const override;
    };
}

#endif // OPENTERA_WEBRTC_NATIVE_CLIENT_SOURCES_ROS_VIDEO_SOURCE_H
