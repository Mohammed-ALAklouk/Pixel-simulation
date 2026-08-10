#pragma once
#include "rgb.h"
#include "MaterialData.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace scree
{
	struct Block
	{
		MaterialID id;
		RGB color;
		std::uint8_t lifespan = 255;
	};
}