#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

#include <iostream>
#include <string>

using namespace cv;

typedef struct _SrcData {
    GstElement* pipeline;
    GstElement* source;
    GstElement* convert;
    GstElement* sink;
} SrcData;

typedef struct _DstData {
    GstElement* pipeline;
    GstElement* appsrc;
    GstElement* convert;
    GstElement* encoder; // Encoder to link videoconvert and payloader
    GstElement* payloader;
    GstElement* udpsink;
} DstData;

GstBuffer *mat_to_gst_buffer(const Mat &frame) {
    gsize size = frame.step[0] * frame.rows;

    GstBuffer *buffer = gst_buffer_new_allocate(NULL, size, NULL);

    if (!buffer) {
        std::cout << "Couldn't create buffer!" << std::endl;
        return nullptr;
    }

    GstMapInfo map;

    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        std::cout << "Couldn't map buffer!" << std::endl;
        return nullptr;
    }

    if (frame.isContinuous()) {
        memcpy(map.data, frame.data, size);
    } else {
        size_t row_size = frame.cols * frame.elemSize();
        for (int i = 0; i < frame.rows; ++i) {
            memcpy(map.data + i * row_size, frame.ptr(i), row_size);
        }
    }

    gst_buffer_unmap(buffer, &map);
    return buffer;
}

GstSample *mat_to_gst_sample(const Mat &frame, GstCaps *caps) {
    GstBuffer *buffer = mat_to_gst_buffer(frame);

    if (!buffer) {
        return nullptr;
    }

    GstClockTime timestamp = gst_util_get_timestamp();

    GST_BUFFER_PTS(buffer) = timestamp;
    GST_BUFFER_DTS(buffer) = timestamp;

    GstSample *sample = gst_sample_new(buffer, caps, NULL, NULL);

    gst_buffer_unref(buffer);

    return sample;
}

Mat gst_sample_to_mat(GstSample* sample) {
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        std::cerr << "Cannot map gstreamer buffer" << std::endl;
        return Mat();
    }
    
    GstCaps* caps = gst_sample_get_caps(sample);
    GstStructure* structure = gst_caps_get_structure(caps, 0);
    
    int width, height;
    gst_structure_get_int(structure, "width", &width);
    gst_structure_get_int(structure, "height", &height);

    const char* format = gst_structure_get_string(structure, "format");
    int type;

    if (g_str_equal(format, "RGB")) {
        type = CV_8UC3;
    } else if (g_str_equal(format, "BGR")) {
        type = CV_8UC3;
    } else if (g_str_equal(format, "RGBA")) {
        type = CV_8UC4;
    } else if (g_str_equal(format, "BGRA")) {
        type = CV_8UC4;
    } else if (g_str_equal(format, "GRAY8")) {
        type = CV_8UC1;
    } else {
        type = CV_8UC3;
    }

    Mat frame(height, width, type, (void*)map.data);

    if (g_str_equal(format, "RGB")) {
        cvtColor(frame, frame, COLOR_RGB2BGR);
    }

    Mat result = frame.clone();

    gst_buffer_unmap(buffer, &map);
    
    return result;
}

int main(int argc, char** argv) {
    gst_init(&argc, &argv);

    SrcData src_data;
    DstData dst_data;

    // Source pipeline
    src_data.pipeline = gst_pipeline_new("src_pipeline");
    src_data.source = gst_element_factory_make("v4l2src", "src_source");
    src_data.convert = gst_element_factory_make("videoconvert", "src_convert");
    src_data.sink = gst_element_factory_make("appsink", "src_sink");

    // Dst pipeline
    dst_data.pipeline = gst_pipeline_new("dst_pipeline");
    dst_data.appsrc = gst_element_factory_make("appsrc", "dst_source");
    dst_data.convert = gst_element_factory_make("videoconvert", "dst_convert");
    dst_data.encoder = gst_element_factory_make("x264enc", "dst_encoder"); // Encoder to link videoconvert and payloader
    dst_data.payloader = gst_element_factory_make("rtph264pay", "dst_payloader");
    dst_data.udpsink = gst_element_factory_make("udpsink", "dst_udpsink");

    if (!src_data.pipeline || !src_data.source || !src_data.convert || !src_data.sink) {
        std::cerr << "Couldn't create src elements." << std::endl;
        return -1;
    }

    if (!dst_data.pipeline || !dst_data.appsrc || !dst_data.convert|| !dst_data.encoder || !dst_data.payloader || !dst_data.udpsink) {
        std::cerr << "Couldn't create dst elements." << std::endl;
        return -1;
    }

    // Config appsink
    g_object_set(G_OBJECT(src_data.sink), "emit-signals", TRUE, NULL);
    g_object_set(G_OBJECT(src_data.sink), "drop", TRUE, NULL);

    // Config appsrc
    g_object_set(G_OBJECT(dst_data.appsrc), "stream-type", 0, NULL);
    g_object_set(G_OBJECT(dst_data.appsrc), "format", GST_FORMAT_TIME, NULL);
    g_object_set(G_OBJECT(dst_data.appsrc), "is-live", TRUE, NULL);

    // Config encoder
    g_object_set(G_OBJECT(dst_data.encoder), "tune", 4, NULL);  // zerolatency preset
    g_object_set(G_OBJECT(dst_data.encoder), "speed-preset", 1, NULL);  // ultrafast
    g_object_set(G_OBJECT(dst_data.encoder), "bitrate", 500, NULL);  // 500 kbps

    // Config UDP-sink
    g_object_set(G_OBJECT(dst_data.udpsink), "host", "192.168.1.100", NULL);
    g_object_set(G_OBJECT(dst_data.udpsink), "port", 5000, NULL);

    GstCaps* caps = gst_caps_new_simple("video/x-raw",
        "format", G_TYPE_STRING, "RGB",
        "width", G_TYPE_INT, 640,
        "height", G_TYPE_INT, 480,
        "framerate", GST_TYPE_FRACTION, 30, 1,
        NULL);

    gst_app_sink_set_caps(GST_APP_SINK(src_data.sink), caps);
    g_object_set(G_OBJECT(dst_data.appsrc), "caps", caps, NULL);

    gst_bin_add_many(GST_BIN(src_data.pipeline), src_data.source, src_data.convert, src_data.sink, NULL);
    gst_bin_add_many(GST_BIN(dst_data.pipeline), dst_data.appsrc, dst_data.convert, dst_data.encoder, dst_data.payloader, dst_data.udpsink , NULL);

    if (!gst_element_link_many(src_data.source, src_data.convert, src_data.sink, NULL)) {
        std::cerr << "Couldn't link gstreamer elements" << std::endl;
        return -1;
    }

    if (!gst_element_link_many(dst_data.appsrc, dst_data.convert, dst_data.encoder, dst_data.payloader, dst_data.udpsink , NULL)) {
        std::cerr << "Couldn't link dst elements" << std::endl;
        return -1;
    }

    gst_element_set_state(src_data.pipeline, GST_STATE_PLAYING);
    gst_element_set_state(dst_data.pipeline, GST_STATE_PLAYING);

    namedWindow("GStreamer to OpenCV", WINDOW_AUTOSIZE);

    while (true) {
        GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(src_data.sink));
        
        if (!sample) {
            std::cerr << "Couldn't acquire sample" << std::endl;
            break;
        }

        Mat frame = gst_sample_to_mat(sample);
        gst_sample_unref(sample);
        
        if (frame.empty()) {
            std::cerr << "Empty frame!" << std::endl;
            continue;
        }

        Mat processed_frame;
        bitwise_not(frame, processed_frame);

        imshow("GStreamer to OpenCV", processed_frame);

        int key = waitKey(30);
        if (key == 27)
            break;

        GstSample* out_sample = mat_to_gst_sample(processed_frame, caps);
    
        if (out_sample) {
            GstFlowReturn ret = gst_app_src_push_sample(GST_APP_SRC(dst_data.appsrc), out_sample);
            gst_sample_unref(out_sample);
            
            if (ret != GST_FLOW_OK) {
                std::cerr << "Error during sending frame to appsrc: " << ret << std::endl;
                break;
            }
        }
    }


    destroyAllWindows();
    gst_caps_unref(caps);
    gst_element_set_state(src_data.pipeline, GST_STATE_NULL);
    gst_element_set_state(dst_data.pipeline, GST_STATE_NULL);
    gst_object_unref(GST_OBJECT(src_data.pipeline));
    gst_object_unref(GST_OBJECT(dst_data.pipeline));
    
    return 0;
}