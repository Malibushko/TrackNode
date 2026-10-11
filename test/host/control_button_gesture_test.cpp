#include "infrastructure/control_button_gesture.h"
#include <stdexcept>

void require(bool condition)
{
	if (!condition)
	{
		throw std::runtime_error("Control button gesture test failed");
	}
}

void require_gesture(const std::optional<control_button_gesture> &event,
	control_button_gesture::kind type, std::uint32_t duration_ms)
{
	require(event && event->type == type && event->duration_ms == duration_ms);
}

int main()
{
	constexpr std::uint32_t pairing_thresholds[]{5000};
	control_button_gesture_recognizer gesture{30, pairing_thresholds};
	require(!gesture.update(true, 100));
	require(!gesture.update(false, 110));
	require(!gesture.update(true, 120));
	require(!gesture.update(true, 149));
	require(!gesture.update(true, 150));
	require(!gesture.update(true, 5149));
	require_gesture(gesture.update(true, 5150), control_button_gesture::kind::hold_threshold_reached, 5000);
	require(!gesture.update(true, 12000));
	require(!gesture.update(false, 12001));
	require_gesture(gesture.update(false, 12031), control_button_gesture::kind::released, 11851);
	require(!gesture.update(true, 12040));
	require(!gesture.update(true, 12070));
	require(!gesture.update(false, 15000));
	require_gesture(gesture.update(false, 15030), control_button_gesture::kind::released, 2930);
	require(!gesture.update(true, 16000));
	require(!gesture.update(true, 16030));
	require_gesture(gesture.update(true, 21030), control_button_gesture::kind::hold_threshold_reached, 5000);

	control_button_gesture_recognizer wrap{30, pairing_thresholds};
	require(!wrap.update(true, UINT32_MAX - 100));
	require(!wrap.update(true, UINT32_MAX - 70));
	require(!wrap.update(true, 4928));
	require_gesture(wrap.update(true, 4929), control_button_gesture::kind::hold_threshold_reached, 5000);
	require(!wrap.update(false, 4930));
	require(!wrap.update(true, 4931));
	require(!wrap.update(true, 10000));

	constexpr std::uint32_t multiple_thresholds[]{2000, 5000};
	control_button_gesture_recognizer multiple{30, multiple_thresholds};
	require(!multiple.update(true, 0));
	require(!multiple.update(true, 30));
	require_gesture(multiple.update(true, 2030), control_button_gesture::kind::hold_threshold_reached, 2000);
	require(!multiple.update(true, 3030));
	require_gesture(multiple.update(true, 5030), control_button_gesture::kind::hold_threshold_reached, 5000);
	require(!multiple.update(true, 6030));
	require(!multiple.update(false, 7000));
	require_gesture(multiple.update(false, 7030), control_button_gesture::kind::released, 6970);
	require(!multiple.update(true, 8000));
	require(!multiple.update(true, 8030));
	require_gesture(multiple.update(true, 10030), control_button_gesture::kind::hold_threshold_reached, 2000);
	require(!multiple.update(false, 11030));
	require(!multiple.update(true, 11040));
	require(!multiple.update(false, 12030));
	require_gesture(multiple.update(false, 12060), control_button_gesture::kind::released, 4000);
	require(!multiple.update(false, 16000));
}
