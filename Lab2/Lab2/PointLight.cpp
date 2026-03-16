#include "PointLight.h"

void PointLight::Fill(PointLightGPU& out) const
{
    out.Position = m_position;
    out.Range = m_range;
    out.Color = m_color;
    out.Intensity = m_intensity;
}