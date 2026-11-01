// SPDX-License-Identifier: MIT
// Optional diagnostic using the existing UE OpenCV SDK. No MediaPipe, network, or recording.
#include <windows.h>
#include <opencv2/opencv.hpp>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
int main() {
    std::atomic<bool> done{false};
    std::thread watchdog([&] {
        for (int i=0; i<100 && !done; ++i) Sleep(100);
        if (!done) TerminateProcess(GetCurrentProcess(), 3);
    });
    int result = 1;
    try {
        std::cerr << "Opening OpenCV CAP_DSHOW camera 0\n";
        cv::VideoCapture capture(0, cv::CAP_DSHOW);
        if (capture.isOpened()) {
            capture.set(cv::CAP_PROP_FRAME_WIDTH, 640); capture.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
            cv::Mat frame; int frames = 0;
            auto begin = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now()-begin < std::chrono::seconds(3)) {
                if (!capture.read(frame) || frame.empty()) throw std::runtime_error("DirectShow returned no image");
                ++frames;
            }
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
            std::cout << "{\"backend\":\"CAP_DSHOW\",\"frames\":" << frames << ",\"width\":" << frame.cols << ",\"height\":" << frame.rows << ",\"fps\":" << frames/seconds << "}\n";
            result = 0;
        } else std::cerr << "DirectShow could not open camera 0\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; }
    done = true; watchdog.join(); return result;
}
