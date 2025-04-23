#include <opencv2/opencv.hpp>
#include <opencv2/imgproc/imgproc.hpp>


/**
 * Приводит изображение к квадратной форме, выравнивая меньшую размерность по большей.
 * 
 * @param frame Входное изображение (кадр)
 * @param method Метод выравнивания:
 *               1 - равномерное добавление черных полос с обеих сторон
 *               2 - добавление черных полос только с одной стороны
 *               3 - растягивание изображения
 * @return Квадратный кадр
 */


cv::Mat squareFrame(const cv::Mat& frame, int method = 1) {
    int height = frame.rows;
    int width = frame.cols;
    
    // Определяем большую из размерностей
    int maxDimension = std::max(height, width);
    
    // Если кадр уже квадратный, возвращаем копию исходного кадра
    if (height == width) {
        return frame.clone();
    }
    
    cv::Mat result;
    
    if (method == 1) {
        // Метод 1: равномерное добавление черных полос
        if (width < height) {
            // Добавляем черные полосы слева и справа
            int padding = (height - width) / 2;
            int paddingRight = (height - width) - padding; // для случая нечетной разницы
            
            cv::copyMakeBorder(frame, result, 0, 0, padding, paddingRight, 
                            cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
        } else {
            // Добавляем черные полосы сверху и снизу
            int padding = (width - height) / 2;
            int paddingBottom = (width - height) - padding; // для случая нечетной разницы
            
            cv::copyMakeBorder(frame, result, padding, paddingBottom, 0, 0, 
                            cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
        }
    } else if (method == 2) {
        // Метод 2: добавление черных полос только с одной стороны
        if (width < height) {
            // Добавляем черные полосы только справа
            cv::copyMakeBorder(frame, result, 0, 0, 0, height - width, 
                            cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
        } else {
            // Добавляем черные полосы только снизу
            cv::copyMakeBorder(frame, result, 0, width - height, 0, 0, 
                            cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
        }
    } else if (method == 3) {
        // Метод 3: растягивание изображения
        cv::resize(frame, result, cv::Size(maxDimension, maxDimension), 0, 0, cv::INTER_LINEAR);
    } else {
        // Неверный метод, возвращаем исходное изображение
        return frame.clone();
    }
    
    return result;
}