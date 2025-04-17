#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include <iostream>
#include <string>

using namespace cv;

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

    GstElement* pipeline = gst_pipeline_new("pipeline");
    GstElement* source = gst_element_factory_make("v4l2src", "source");
    GstElement* convert = gst_element_factory_make("videoconvert", "convert");
    GstElement* sink = gst_element_factory_make("appsink", "sink");
    
    if (!pipeline || !source || !convert || !sink) {
        std::cerr << "Couldn't create gstreamer elements." << std::endl;
        return -1;
    }

    g_object_set(G_OBJECT(sink), "emit-signals", TRUE, NULL);
    g_object_set(G_OBJECT(sink), "drop", TRUE, NULL);

    GstCaps* caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "BGR",
                                        NULL);
    gst_app_sink_set_caps(GST_APP_SINK(sink), caps);
    gst_caps_unref(caps);

    gst_bin_add_many(GST_BIN(pipeline), source, convert, sink, NULL);

    if (!gst_element_link_many(source, convert, sink, NULL)) {
        std::cerr << "Couldn't cat gstreamer elements" << std::endl;
        return -1;
    }

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    namedWindow("GStreamer to OpenCV", WINDOW_AUTOSIZE);

    while (true) {
        GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
        
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

        imshow("GStreamer to OpenCV", frame);

        int key = waitKey(30);
        if (key == 27)
            break;
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(GST_OBJECT(pipeline));
    
    return 0;
}