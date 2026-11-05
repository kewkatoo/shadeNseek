#include "ShadeSunSubsystem.h"
#include "shadeNseek.h"
#include "ShadeTypes.h"
#include "Engine/DirectionalLight.h"
#include "Engine/TargetPoint.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"

UShadeSunSubsystem* UShadeSunSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UShadeSunSubsystem>() : nullptr;
}

ADirectionalLight* UShadeSunSubsystem::GetSun() const
{
	if (Sun.IsValid())
	{
		return Sun.Get();
	}
	ADirectionalLight* Fallback = nullptr;
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		UDirectionalLightComponent* C = Cast<UDirectionalLightComponent>(It->GetLightComponent());
		if (C && C->IsUsedAsAtmosphereSunLight())
		{
			Sun = *It;
			return *It;
		}
		Fallback = Fallback ? Fallback : *It;
	}
	Sun = Fallback;
	return Fallback;
}

FVector UShadeSunSubsystem::GetToSun() const
{
	if (const ADirectionalLight* L = GetSun())
	{
		return -L->GetActorForwardVector();
	}
	return FVector(0.f, 0.f, 1.f);
}

bool UShadeSunSubsystem::IsSunlit(const FVector& Point, const AActor* IgnoreA, const AActor* IgnoreB) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const FVector Dir = GetToSun();
	if (Dir.Z <= 0.02f)
	{
		return false; // sun below the horizon
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShadeSun), false);
	if (IgnoreA) { Params.AddIgnoredActor(IgnoreA); }
	if (IgnoreB) { Params.AddIgnoredActor(IgnoreB); }
	return !World->LineTraceTestByChannel(Point + Dir * 2.f, Point + Dir * TraceDistance, ECC_Sunlight, Params);
}

float UShadeSunSubsystem::SunlitFraction(const TArray<FVector>& Points, const AActor* IgnoreA, const AActor* IgnoreB) const
{
	if (Points.Num() == 0)
	{
		return 0.f;
	}
	int32 Lit = 0;
	for (const FVector& P : Points)
	{
		Lit += IsSunlit(P, IgnoreA, IgnoreB) ? 1 : 0;
	}
	return static_cast<float>(Lit) / Points.Num();
}

bool UShadeSunSubsystem::WriteSunMap(const FBox2D& Area, float StepCm, float TopZ, const FString& Path) const
{
	UWorld* World = GetWorld();
	if (!World || StepCm <= 1.f)
	{
		return false;
	}
	const int32 W = FMath::Clamp(FMath::CeilToInt((Area.Max.X - Area.Min.X) / StepCm), 8, 4096);
	const int32 H = FMath::Clamp(FMath::CeilToInt((Area.Max.Y - Area.Min.Y) / StepCm), 8, 4096);
	TArray<FColor> Pixels;
	Pixels.SetNumZeroed(W * H);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShadeSunMap), true);
	for (int32 Iy = 0; Iy < H; ++Iy)
	{
		for (int32 Ix = 0; Ix < W; ++Ix)
		{
			// Image X = world X (east), image rows = world Y (top row = max Y so north is up when Y is "left").
			const FVector2D P(Area.Min.X + (Ix + 0.5f) * StepCm, Area.Max.Y - (Iy + 0.5f) * StepCm);
			FHitResult Hit;
			FColor C(0, 0, 0, 255);
			if (World->LineTraceSingleByChannel(Hit, FVector(P, TopZ), FVector(P, -2000.f), ECC_Visibility, Params))
			{
				const bool bLit = IsSunlit(Hit.ImpactPoint + FVector(0, 0, 10.f));
				const float Height = FMath::Clamp((Hit.ImpactPoint.Z + 200.f) / 1400.f, 0.f, 1.f);
				C = bLit ? FColor(255, uint8(190 + 50 * Height), uint8(120 + 60 * Height), 255)
					: FColor(uint8(30 + 40 * Height), uint8(45 + 50 * Height), uint8(90 + 80 * Height), 255);
			}
			Pixels[Iy * W + Ix] = C;
		}
	}
	for (TActorIterator<ATargetPoint> It(World); It; ++It)
	{
		bool bAP = false;
		for (const FName& T : It->Tags) { bAP |= T.ToString().StartsWith(TEXT("AP_")); }
		if (!bAP) { continue; }
		const FVector L = It->GetActorLocation();
		const int32 Cx = FMath::FloorToInt((L.X - Area.Min.X) / StepCm);
		const int32 Cy = FMath::FloorToInt((Area.Max.Y - L.Y) / StepCm);
		for (int32 Dy = -2; Dy <= 2; ++Dy)
		{
			for (int32 Dx = -2; Dx <= 2; ++Dx)
			{
				const int32 X = Cx + Dx, Y = Cy + Dy;
				if (X >= 0 && Y >= 0 && X < W && Y < H) { Pixels[Y * W + X] = FColor(255, 20, 20, 255); }
			}
		}
	}

	IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Png = Mod.CreateImageWrapper(EImageFormat::PNG);
	if (!Png.IsValid() || !Png->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), W, H, ERGBFormat::BGRA, 8))
	{
		return false;
	}
	const TArray64<uint8>& Data = Png->GetCompressed();
	const bool bOk = FFileHelper::SaveArrayToFile(Data, *Path);
	UE_LOG(LogShade, Log, TEXT("SHADE_SUNMAP %s %dx%d step %.0f area (%.0f,%.0f)-(%.0f,%.0f) ok=%d"), *Path, W, H, StepCm,
		Area.Min.X, Area.Min.Y, Area.Max.X, Area.Max.Y, bOk ? 1 : 0);
	return bOk;
}
