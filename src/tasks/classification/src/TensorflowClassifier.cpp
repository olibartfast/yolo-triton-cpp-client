#include "TensorflowClassifier.hpp"

TensorflowClassifier::TensorflowClassifier(const TritonModelInfo& modelInfo)
    : TaskInterface(modelInfo) {
}


std::vector<uint8_t> TensorflowClassifier::preprocess_image(const cv::Mat& img, const std::string& format,
                                                     int img_type1, int img_type3, size_t img_channels,
                                                     const cv::Size& img_size)
{
    cv::Mat sample;
    img.convertTo(sample, (img_channels == 3) ? img_type3 : img_type1);
    cv::resize(sample, sample, cv::Size(img_size.width, img_size.height));

    // Allocate a buffer to hold all image elements
    size_t img_byte_size = sample.total() * sample.elemSize();
    size_t pos = 0;
    std::vector<uint8_t> input_data(img_byte_size);

    // Copy data maintaining NHWC format
    if (sample.isContinuous()) {
        memcpy(&(input_data[0]), sample.datastart, img_byte_size);
        pos = img_byte_size;
    } else {
        size_t row_byte_size = sample.cols * sample.elemSize();
        for (int r = 0; r < sample.rows; ++r) {
            memcpy(&(input_data[pos]), sample.ptr<uint8_t>(r), row_byte_size);
            pos += row_byte_size;
        }
    }

    return input_data;
}

std::vector<std::vector<uint8_t>> TensorflowClassifier::preprocess(const std::vector<cv::Mat>& imgs)
{
    if (imgs.empty()) {
        throw std::runtime_error("Input image vector is empty");
    }

    cv::Mat img = imgs.front();
    if (img.empty()) {
        throw std::runtime_error("Input image is empty");
    }
    std::vector<std::vector<uint8_t>> input_data(model_info_.input_shapes.size());

    for (size_t i = 0; i < model_info_.input_shapes.size(); ++i) {
        const auto& input_name = model_info_.input_names[i];
        const auto& input_shape = model_info_.input_shapes[i];
        const auto& input_format = model_info_.input_formats[i];
        const auto& input_type = model_info_.input_types[i];

        if (input_shape.size() >= 3) {
            // This is likely an image input
            const auto input_size = cv::Size(input_width_, input_height_);
            input_data[i] = preprocess_image(img, input_format, model_info_.type1_, model_info_.type3_, img.channels(), input_size);
        } else {
            // For other types of inputs, you might need to add more cases
            // or use a default handling method
            throw std::runtime_error("Unhandled input");
        }
    }
    return input_data;
}

std::vector<Result> TensorflowClassifier::postprocess(const cv::Size& frame_size,
                                                      const std::vector<std::vector<TensorElement>>& infer_results,
                                                      const std::vector<std::vector<int64_t>>& infer_shapes) {
    // Check if the input vectors are not empty
    if (infer_results.empty() || infer_shapes.empty()) {
        throw std::runtime_error("Inference results or shapes are empty.");
    }

    // Get the first set of results and shape
    const auto& results = infer_results.front();
    const auto shape = infer_shapes[0][1];

    // Define the confidence threshold
    const float threshold = 0.5f;

    // Create a vector of indices and sort them based on the result values
    std::vector<int> indices(shape);
    std::iota(indices.begin(), indices.end(), 0);
    std::sort(indices.begin(), indices.end(), [&results](int i1, int i2) {
        return std::visit([](auto&& arg1, auto&& arg2) { return arg1 > arg2; },
                          results[i1], results[i2]);
    });

    // Extract results with confidence greater than the threshold
    std::vector<Result> classification_results;
    for (size_t i = 0; i < shape; ++i) {
        float confidence = std::visit([](auto&& arg) -> float { return static_cast<float>(arg); },
                                      results[indices[i]]);
        if (confidence <= threshold) break;

        Classification classification;
        classification.class_id = indices[i];
        classification.class_confidence = confidence;
        classification_results.emplace_back(classification);
    }

    return classification_results;
}