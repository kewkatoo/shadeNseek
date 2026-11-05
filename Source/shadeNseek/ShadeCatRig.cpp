#include "ShadeCatRig.h"
#include "shadeNseek.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "ReferenceSkeleton.h"

namespace
{
	const TCHAR* GBoneNames[CatBone::Count] = {
		TEXT("root"), TEXT("pelvis"), TEXT("spine_01"), TEXT("spine_02"), TEXT("neck"), TEXT("head"), TEXT("ear_l"), TEXT("ear_r"),
		TEXT("upperarm_l"), TEXT("forearm_l"), TEXT("hand_l"), TEXT("upperarm_r"), TEXT("forearm_r"), TEXT("hand_r"),
		TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"),
		TEXT("tail_01"), TEXT("tail_02"), TEXT("tail_03"), TEXT("tail_04"), TEXT("tail_05"),
	};

	float Frac(float X) { return X - FMath::FloorToFloat(X); }
}

UShadeCatRig::UShadeCatRig()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	for (int32& I : BoneIndex) { I = INDEX_NONE; }
}

void UShadeCatRig::Init(UPoseableMeshComponent* InMesh)
{
	Mesh = InMesh;
	bReady = false;
	if (!InMesh || !InMesh->GetSkinnedAsset())
	{
		UE_LOG(LogShade, Warning, TEXT("CatRig: no skinned asset"));
		return;
	}
	const FReferenceSkeleton& Ref = InMesh->GetSkinnedAsset()->GetRefSkeleton();
	RefLocal = Ref.GetRefBonePose();
	const int32 N = Ref.GetNum();
	Parents.SetNum(N);
	RefComp.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		Parents[i] = Ref.GetParentIndex(i);
		RefComp[i] = Parents[i] >= 0 ? RefLocal[i] * RefComp[Parents[i]] : RefLocal[i];
	}
	int32 Missing = 0;
	for (int32 b = 0; b < CatBone::Count; ++b)
	{
		BoneIndex[b] = Ref.FindBoneIndex(FName(GBoneNames[b]));
		Missing += BoneIndex[b] == INDEX_NONE ? 1 : 0;
	}
	if (Missing > 0 || BoneIndex[CatBone::Head] == INDEX_NONE || BoneIndex[CatBone::Pelvis] == INDEX_NONE)
	{
		UE_LOG(LogShade, Warning, TEXT("CatRig: %d bones missing from skeleton (%d total)"), Missing, N);
		if (BoneIndex[CatBone::Head] == INDEX_NONE || BoneIndex[CatBone::Pelvis] == INDEX_NONE)
		{
			return;
		}
	}

	// Cat space from the skeleton itself.
	AxU = FVector::UpVector;
	FVector F = RefComp[BoneIndex[CatBone::Head]].GetLocation() - RefComp[BoneIndex[CatBone::Pelvis]].GetLocation();
	F.Z = 0.f;
	AxF = F.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	AxR = FVector::CrossProduct(AxU, AxF);
	PitchAxis = FVector::CrossProduct(AxF, AxU);
	YawAxis = AxU;
	RollAxis = FVector::CrossProduct(AxU, AxR);
	FacingYaw = -FMath::RadiansToDegrees(FMath::Atan2(AxF.Y, AxF.X));

	bReady = true;
	UE_LOG(LogShade, Log, TEXT("CatRig ready: %d bones, forward (%.2f %.2f %.2f), facing yaw %.1f"), N, AxF.X, AxF.Y, AxF.Z, FacingYaw);
}

FVector UShadeCatRig::GetRestBoneLocation(CatBone::Type Bone) const
{
	const int32 I = BoneIndex[Bone];
	return (bReady && RefComp.IsValidIndex(I)) ? RefComp[I].GetLocation() : FVector::ZeroVector;
}

void UShadeCatRig::SetPose(ECatPose Pose, float BlendTime)
{
	TargetPose = Pose;
	BlendRate = 1.f / FMath::Max(BlendTime, 0.05f);
}

void UShadeCatRig::Land(float Strength)
{
	Squash.Velocity -= 6.f * FMath::Clamp(Strength, 0.f, 1.5f);
}

FQuat UShadeCatRig::ToQuat(const FVector& PYR) const
{
	return FQuat(YawAxis, FMath::DegreesToRadians(PYR.Y)) * FQuat(PitchAxis, FMath::DegreesToRadians(PYR.X)) * FQuat(RollAxis, FMath::DegreesToRadians(PYR.Z));
}

void UShadeCatRig::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bReady || !Mesh.IsValid())
	{
		return;
	}
	const float Dt = FMath::Clamp(DeltaTime, 0.f, 0.05f);
	Time += Dt;

	// Pose weights move linearly toward the target (sum stays 1).
	for (int32 i = 0; i < static_cast<int32>(ECatPose::Count); ++i)
	{
		const float Target = (i == static_cast<int32>(TargetPose)) ? 1.f : 0.f;
		PoseW[i] = FMath::FInterpConstantTo(PoseW[i], Target, Dt, BlendRate);
	}

	FCatPoseSample S;
	Evaluate(Dt, S);
	Apply(S);
}

void UShadeCatRig::Evaluate(float Dt, FCatPoseSample& Out)
{
	using namespace CatBone;
	Out.Reset();

	const float LocoW = PoseW[static_cast<int32>(ECatPose::Locomote)];
	const float PushW = PoseW[static_cast<int32>(ECatPose::Push)];
	const float SitW = PoseW[static_cast<int32>(ECatPose::Sit)];
	const float SleepW = PoseW[static_cast<int32>(ECatPose::Sleep)];

	// Locomotion layer (also drives the push gait).
	{
		FCatPoseSample L;
		PoseLocomote(Dt, L, PushW > 0.5f ? 0.55f : 1.f);
		const float W = LocoW + PushW;
		Out.AddScaled(L, W);
		if (PushW > 0.f)
		{
			FCatPoseSample P;
			// Head down, shoulders braced, body low: shoving with the forehead like a cat into a box.
			P.Ang[Spine2] = FVector(-7.f, 0.f, 0.f);
			P.Ang[Neck] = FVector(-20.f, 0.f, 0.f);
			P.Ang[Head] = FVector(-10.f, 0.f, 0.f);
			P.Ang[UArmL] = P.Ang[UArmR] = FVector(10.f, 0.f, 0.f);
			P.Ang[Tail1] = FVector(-22.f, 0.f, 0.f);
			P.Pelvis = FVector(0.f, 0.f, -3.f);
			Out.AddScaled(P, PushW);
		}
	}
	if (SitW > 0.f)
	{
		FCatPoseSample P;
		PoseSit(P);
		Out.AddScaled(P, SitW);
	}
	if (SleepW > 0.f)
	{
		FCatPoseSample P;
		PoseSleep(P);
		Out.AddScaled(P, SleepW);
	}

	// Landing squash (spring) on top of everything.
	const float Sq = Squash.Update(0.f, 140.f, 16.f, Dt);
	Out.Pelvis.Z += Sq * 4.f;
	Out.Ang[ThighL].X += -Sq * 18.f;
	Out.Ang[ThighR].X += -Sq * 18.f;
	Out.Ang[CalfL].X += Sq * 26.f;
	Out.Ang[CalfR].X += Sq * 26.f;
	Out.Ang[UArmL].X += -Sq * 10.f;
	Out.Ang[UArmR].X += -Sq * 10.f;
	Out.Ang[FArmL].X += Sq * 16.f;
	Out.Ang[FArmR].X += Sq * 16.f;

	// Look-at for head + neck (additive, clamped), suppressed while sleeping.
	float WantYaw = 0.f, WantPitch = 0.f;
	if (bHasLook && Mesh.IsValid())
	{
		const FTransform HeadW = Mesh->GetComponentTransform();
		const FVector Local = HeadW.InverseTransformPosition(LookAtWorld) - RefComp[BoneIndex[Head]].GetLocation();
		const FVector Cat(FVector::DotProduct(Local, AxF), FVector::DotProduct(Local, AxR), FVector::DotProduct(Local, AxU));
		WantYaw = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Cat.Y, FMath::Max(Cat.X, 1.f))), -55.f, 55.f);
		WantPitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Cat.Z, FVector2D(Cat.X, Cat.Y).Size())), -30.f, 35.f);
	}
	const float LookW = 1.f - SleepW;
	HeadYaw.Update(WantYaw * LookW, 40.f, 12.f, Dt);
	HeadPitch.Update(WantPitch * LookW, 40.f, 12.f, Dt);
	Out.Ang[Neck] += FVector(HeadPitch.Value * 0.4f, HeadYaw.Value * 0.45f, 0.f);
	Out.Ang[Head] += FVector(HeadPitch.Value * 0.6f, HeadYaw.Value * 0.55f, -HeadYaw.Value * 0.12f);

	// Yawn: head tips up, ears back.
	YawnTime += Dt;
	if (YawnTime < 2.4f)
	{
		const float Y = ShadeEase::Bump(YawnTime, 0.f, 2.4f);
		Out.Ang[Neck].X += 18.f * Y;
		Out.Ang[Head].X += 22.f * Y;
		Out.Ang[EarL] += FVector(-25.f, -10.f, 0.f) * Y;
		Out.Ang[EarR] += FVector(-25.f, 10.f, 0.f) * Y;
	}

	// Ear twitches (always alive, even asleep).
	EarTimer -= Dt;
	if (EarTimer <= 0.f)
	{
		EarTimer = FMath::FRandRange(1.6f, 5.f);
		EarTwitch[FMath::RandRange(0, 1)] = 1.f;
	}
	for (int32 e = 0; e < 2; ++e)
	{
		EarTwitch[e] = FMath::Max(0.f, EarTwitch[e] - Dt * 5.f);
		const float T = FMath::Sin(EarTwitch[e] * PI * 2.f) * EarTwitch[e];
		Out.Ang[e == 0 ? EarL : EarR] += FVector(-14.f * T, (e == 0 ? -1.f : 1.f) * 12.f * T, 0.f);
	}

	// Breathing.
	Breath = FMath::Sin(Time * (SleepW > 0.5f ? 1.5f : 2.6f));
	Out.Ang[Spine2].X += Breath * (0.5f + 0.8f * SleepW);
	Out.Ang[Spine1].X -= Breath * 0.4f * SleepW;
}

void UShadeCatRig::PoseLocomote(float Dt, FCatPoseSample& S, float StrideScale)
{
	using namespace CatBone;
	const float Ground = bGrounded ? 1.f : 0.f;
	Air = ShadeEase::Approach(Air, 1.f - Ground, bGrounded ? 14.f : 8.f, Dt);

	const float Sp = Speed / FMath::Max(MeshScale, 0.2f);
	Amp = ShadeEase::Approach(Amp, FMath::Clamp(Sp / 160.f, 0.f, 1.f), 8.f, Dt);
	Sprint = ShadeEase::Approach(Sprint, FMath::Clamp((Sp - 360.f) / 260.f, 0.f, 1.f), 5.f, Dt);

	// Stride grows with speed; phase is tied to distance so paws don't skate.
	const float Stride = FMath::Lerp(34.f, 70.f, Sprint) * StrideScale;
	if (bGrounded)
	{
		Phase = Frac(Phase + Sp * Dt / Stride);
	}

	const float Swing = FMath::Lerp(17.f, 30.f, Sprint) * Amp;
	const float Lift = FMath::Lerp(1.f, 1.35f, Sprint) * Amp;
	// Lateral-sequence walk; sprint pulls the pairs together into a bound-like rhythm.
	const float Off[4] = { 0.25f, 0.75f, 0.f, FMath::Lerp(0.5f, 0.1f, Sprint) }; // FL FR HL HR
	const int32 Upper[4] = { UArmL, UArmR, ThighL, ThighR };
	const int32 Mid[4] = { FArmL, FArmR, CalfL, CalfR };
	const int32 Low[4] = { HandL, HandR, FootL, FootR };
	const float Stance = 0.6f;
	for (int32 l = 0; l < 4; ++l)
	{
		const float P = Frac(Phase + Off[l]);
		float Sw, Lf;
		if (P < Stance)
		{
			Sw = FMath::Lerp(1.f, -1.f, P / Stance);
			Lf = 0.f;
		}
		else
		{
			const float Q = (P - Stance) / (1.f - Stance);
			Sw = FMath::Lerp(-1.f, 1.f, ShadeEase::Smooth(Q));
			Lf = FMath::Sin(Q * PI);
		}
		const bool bFront = l < 2;
		if (bFront)
		{
			S.Ang[Upper[l]].X = Sw * Swing;
			S.Ang[Mid[l]].X = -Lf * 55.f * Lift;
			S.Ang[Low[l]].X = Lf * 45.f * Lift - Sw * 6.f * Amp;
		}
		else
		{
			S.Ang[Upper[l]].X = Sw * Swing * 0.9f;
			S.Ang[Mid[l]].X = Lf * 42.f * Lift;
			S.Ang[Low[l]].X = -Lf * 36.f * Lift + Sw * 6.f * Amp;
		}
		// Footstep at touchdown (phase wraps into stance).
		if (bGrounded && Amp > 0.15f && PrevLegPhase[l] > 0.85f && P < 0.15f && OnFootPlant)
		{
			OnFootPlant(l);
		}
		PrevLegPhase[l] = P;
	}

	// Body: bob twice per cycle, gentle roll/yaw, spine flex at sprint.
	const float Cyc = Phase * 2.f * PI;
	S.Pelvis.Z = -(1.f - FMath::Cos(2.f * Cyc)) * (0.5f + 1.2f * Sprint) * Amp;
	S.Ang[Pelvis] = FVector(FMath::Sin(Cyc * 2.f) * 2.f * Sprint, FMath::Sin(Cyc) * 4.f * Amp, FMath::Sin(Cyc) * 3.f * Amp);
	S.Ang[Spine1] = FVector(FMath::Sin(Cyc * 2.f) * 3.f * Sprint, -FMath::Sin(Cyc) * 2.f * Amp, 0.f);
	S.Ang[Spine2] = FVector(-FMath::Sin(Cyc * 2.f) * 3.f * Sprint, -FMath::Sin(Cyc) * 2.5f * Amp, -FMath::Sin(Cyc) * 2.f * Amp);
	// Head stays level and slightly lowered when walking (stalking curiosity).
	S.Ang[Neck] = FVector(-5.f * Amp - 3.f * Sprint, FMath::Sin(Cyc) * 1.5f * Amp, 0.f);
	S.Ang[Head] = FVector(3.f * Amp, 0.f, -FMath::Sin(Cyc) * 2.f * Amp);

	// Turning: bend the spine into the turn.
	Lean.Update(FMath::Clamp(TurnRate * 0.035f, -14.f, 14.f), 60.f, 14.f, Dt);
	S.Ang[Spine1].Y += Lean.Value * 0.5f;
	S.Ang[Spine2].Y += Lean.Value * 0.7f;
	S.Ang[Neck].Y += Lean.Value * 0.6f;
	S.Ang[Pelvis].Z += -Lean.Value * 0.3f;

	// Tail: carriage from excitement, travelling wave.
	const float Carriage = FMath::Lerp(-28.f, 8.f, Excitement) - 10.f * Sprint;
	for (int32 k = 0; k < 5; ++k)
	{
		const float Wave = FMath::Sin(Time * (2.1f + 1.4f * Amp) - k * 0.75f) * (3.5f + 3.f * k) * (0.45f + 0.55f * Amp);
		S.Ang[Tail1 + k] = FVector((k == 0 ? Carriage : 4.f + 3.f * k * Excitement) - 4.f * Sprint, Wave, 0.f);
	}

	// Air: reach forward with the front paws, extend the hind legs back, tail as a counterweight.
	if (Air > 0.01f)
	{
		const float Up = FMath::Clamp(VerticalSpeed / 400.f, -1.f, 1.f);
		const float A = Air;
		S.Ang[UArmL].X = FMath::Lerp(S.Ang[UArmL].X, 38.f + 12.f * Up, A);
		S.Ang[UArmR].X = FMath::Lerp(S.Ang[UArmR].X, 34.f + 12.f * Up, A);
		S.Ang[FArmL].X = FMath::Lerp(S.Ang[FArmL].X, -18.f, A);
		S.Ang[FArmR].X = FMath::Lerp(S.Ang[FArmR].X, -22.f, A);
		S.Ang[ThighL].X = FMath::Lerp(S.Ang[ThighL].X, -34.f, A);
		S.Ang[ThighR].X = FMath::Lerp(S.Ang[ThighR].X, -30.f, A);
		S.Ang[CalfL].X = FMath::Lerp(S.Ang[CalfL].X, -10.f, A);
		S.Ang[CalfR].X = FMath::Lerp(S.Ang[CalfR].X, -12.f, A);
		S.Ang[Spine2].X += (6.f * Up) * A;
		S.Ang[Neck].X += (-4.f * Up) * A;
		S.Ang[Tail1].X = FMath::Lerp(S.Ang[Tail1].X, -12.f - 10.f * Up, A);
	}
}

void UShadeCatRig::PoseSleep(FCatPoseSample& S) const
{
	using namespace CatBone;
	// "Loaf with the chin down": belly on the ground, paws tucked, tail wrapped around the side.
	S.Pelvis = FVector(1.f, 0.f, -11.5f);
	S.Ang[Pelvis] = FVector(-3.f, 0.f, 0.f);
	S.Ang[Spine1] = FVector(-1.f, 4.f, 0.f);
	S.Ang[Spine2] = FVector(-3.f, 5.f, 0.f);
	S.Ang[Neck] = FVector(-26.f, 12.f, 0.f);
	S.Ang[Head] = FVector(4.f, 14.f, -8.f);
	S.Ang[EarL] = FVector(-10.f, -12.f, 0.f);
	S.Ang[EarR] = FVector(-10.f, 12.f, 0.f);
	S.Ang[UArmL] = S.Ang[UArmR] = FVector(-62.f, 0.f, 0.f);
	S.Ang[FArmL] = S.Ang[FArmR] = FVector(118.f, 0.f, 0.f);
	S.Ang[HandL] = S.Ang[HandR] = FVector(-40.f, 0.f, 0.f);
	S.Ang[ThighL] = S.Ang[ThighR] = FVector(58.f, 0.f, 0.f);
	S.Ang[CalfL] = S.Ang[CalfR] = FVector(-98.f, 0.f, 0.f);
	S.Ang[FootL] = S.Ang[FootR] = FVector(52.f, 0.f, 0.f);
	S.Ang[ThighL].Z = -8.f;
	S.Ang[ThighR].Z = 8.f;
	S.Ang[Tail1] = FVector(-62.f, 30.f, 0.f);
	S.Ang[Tail2] = FVector(-6.f, 30.f, 0.f);
	S.Ang[Tail3] = FVector(-2.f, 32.f, 0.f);
	S.Ang[Tail4] = FVector(0.f, 30.f, 0.f);
	S.Ang[Tail5] = FVector(0.f, 26.f, 0.f);
}

void UShadeCatRig::PoseSit(FCatPoseSample& S) const
{
	using namespace CatBone;
	// Upright sit: torso pitched up around the hips, hind legs folded, front legs straight, tail curled in front.
	S.Pelvis = FVector(-3.f, 0.f, -7.f);
	S.Ang[Pelvis] = FVector(34.f, 0.f, 0.f);
	S.Ang[Spine1] = FVector(6.f, 0.f, 0.f);
	S.Ang[Spine2] = FVector(4.f, 0.f, 0.f);
	S.Ang[Neck] = FVector(-30.f, 0.f, 0.f);
	S.Ang[Head] = FVector(-8.f, 0.f, 0.f);
	S.Ang[UArmL] = S.Ang[UArmR] = FVector(-42.f, 0.f, 0.f);
	S.Ang[FArmL] = S.Ang[FArmR] = FVector(2.f, 0.f, 0.f);
	S.Ang[HandL] = S.Ang[HandR] = FVector(-2.f, 0.f, 0.f);
	S.Ang[ThighL] = S.Ang[ThighR] = FVector(30.f, 0.f, 0.f);
	S.Ang[CalfL] = S.Ang[CalfR] = FVector(-112.f, 0.f, 0.f);
	S.Ang[FootL] = S.Ang[FootR] = FVector(48.f, 0.f, 0.f);
	S.Ang[ThighL].Z = -10.f;
	S.Ang[ThighR].Z = 10.f;
	S.Ang[Tail1] = FVector(-90.f, 0.f, 0.f);
	S.Ang[Tail2] = FVector(-10.f, 28.f, 0.f);
	S.Ang[Tail3] = FVector(-4.f, 34.f, 0.f);
	S.Ang[Tail4] = FVector(0.f, 34.f, 0.f);
	S.Ang[Tail5] = FVector(6.f, 28.f, 0.f);
}

void UShadeCatRig::Apply(const FCatPoseSample& S)
{
	UPoseableMeshComponent* M = Mesh.Get();
	if (!M || M->BoneSpaceTransforms.Num() != RefLocal.Num())
	{
		if (M)
		{
			M->BoneSpaceTransforms = RefLocal;
		}
		return;
	}
	for (int32 b = 0; b < CatBone::Count; ++b)
	{
		const int32 I = BoneIndex[b];
		if (I == INDEX_NONE)
		{
			continue;
		}
		const int32 P = Parents[I];
		const FQuat Pq = P >= 0 ? RefComp[P].GetRotation() : FQuat::Identity;
		const FQuat Q = ToQuat(S.Ang[b]);
		FTransform L = RefLocal[I];
		L.SetRotation((Pq.Inverse() * Q * Pq) * RefLocal[I].GetRotation());
		if (b == CatBone::Pelvis)
		{
			L.SetTranslation(RefLocal[I].GetTranslation() + Pq.Inverse().RotateVector(CatToMesh(S.Pelvis)));
		}
		M->BoneSpaceTransforms[I] = L;
	}
	M->MarkRefreshTransformDirty();
	M->RefreshBoneTransforms();
}
