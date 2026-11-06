#include "ShadeSpringArm.h"

FVector UShadeSpringArm::BlendLocations(const FVector& DesiredArmLocation, const FVector& TraceHitLocation, bool bHitSomething, float DeltaTime)
{
	// PreviousArmOrigin holds this frame's (lagged) arm origin: the spring arm stores it right before the sweep.
	const FVector Origin = PreviousArmOrigin;
	const FVector Span = DesiredArmLocation - Origin;
	const float Full = Span.Size();
	if (Full < KINDA_SMALL_NUMBER)
	{
		return DesiredArmLocation;
	}
	const float Want = bHitSomething ? FMath::Min(FVector::Dist(Origin, TraceHitLocation), Full) : Full;
	if (SmoothedLength < 0.f)
	{
		SmoothedLength = Want;
	}
	const float Dt = FMath::Clamp(DeltaTime, 0.f, 0.1f);
	const float Rate = Want < SmoothedLength ? InSpeed : OutSpeed;
	SmoothedLength = FMath::Lerp(SmoothedLength, Want, 1.f - FMath::Exp(-Rate * Dt));
	// Never further out than the clear length, and never more than a few centimetres past what the probe hit.
	SmoothedLength = FMath::Min3(SmoothedLength, Full, Want + 12.f);
	return Origin + Span / Full * SmoothedLength;
}
