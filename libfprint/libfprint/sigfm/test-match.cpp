#include "sigfm.hpp"
#include "img-info.hpp"
#include "binary.hpp"
#include <glib.h>

static SigfmImgInfo sample(bool same_row, int descriptor_offset = 0)
{
    SigfmImgInfo info;
    info.descriptors = cv::Mat::zeros(24, 128, CV_32F);
    for (int i = 0; i < 24; ++i) {
        info.descriptors.at<float>(i, i + descriptor_offset) = 100;
        info.keypoints.emplace_back(cv::Point2f(i * 3, same_row ? 20 : i * 2), 1);
    }
    return info;
}

static void same_row_matches()
{
    auto info = sample(true);
    g_assert_cmpint(sigfm_match_score(&info, &info), >=, 48);
}

static void distinct_rows_match()
{
    auto info = sample(false);
    g_assert_cmpint(sigfm_match_score(&info, &info), >=, 48);
}

static void unrelated_descriptors_fail()
{
    auto enrolled = sample(true);
    auto unrelated = sample(true, 64);
    g_assert_cmpint(sigfm_match_score(&enrolled, &unrelated), ==, 0);
}

static void quarter_turn_matches()
{
    auto enrolled = sample(true);
    auto probe = sample(true);
    for (auto& point : probe.keypoints)
        point.pt = cv::Point2f(80 - point.pt.y, point.pt.x);
    g_assert_cmpint(sigfm_match_score(&enrolled, &probe), >=, 48);
}

static void binary_round_trip()
{
    auto info = sample(true);
    int length;
    auto* bytes = sigfm_serialize_binary(&info, &length);
    auto* copy = sigfm_deserialize_binary(bytes, length);
    g_assert_nonnull(copy);
    g_assert_cmpint(sigfm_match_score(&info, copy), >=, 48);
    sigfm_free_info(copy);
    g_assert_null(sigfm_deserialize_binary(bytes, length - 1));
    free(bytes);
    g_assert_null(sigfm_deserialize_binary(nullptr, 0));
}

static void invalid_matrix_and_vector()
{
    for (int rows : {10, -1, 2000000000}) {
        bin::stream stream;
        stream << int(CV_8UC1) << rows << int(10);
        try {
            cv::Mat matrix;
            stream >> matrix;
            g_assert_not_reached();
        } catch (const std::runtime_error&) {}
    }
    bin::stream stream;
    stream << std::size_t(-1);
    try {
        std::vector<int> values;
        stream >> values;
        g_assert_not_reached();
    } catch (const std::runtime_error&) {}
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/sigfm/same-row", same_row_matches);
    g_test_add_func("/sigfm/distinct-rows", distinct_rows_match);
    g_test_add_func("/sigfm/unrelated", unrelated_descriptors_fail);
    g_test_add_func("/sigfm/quarter-turn", quarter_turn_matches);
    g_test_add_func("/sigfm/binary-round-trip", binary_round_trip);
    g_test_add_func("/sigfm/invalid-binary", invalid_matrix_and_vector);
    return g_test_run();
}
