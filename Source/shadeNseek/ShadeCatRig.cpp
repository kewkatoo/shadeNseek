#include "ShadeCatRig.h"
#include "shadeNseek.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/World.h"
#include "ReferenceSkeleton.h"

namespace
{
	const TCHAR* GBoneNames[CatBone::Count] = {
		TEXT("root"), TEXT("pelvis"), TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03"), TEXT("neck"), TEXT("head"), TEXT("ear_l"), TEXT("ear_r"),
		TEXT("scapula_l"), TEXT("upperarm_l"), TEXT("forearm_l"), TEXT("hand_l"), TEXT("finger_l"),
		TEXT("scapula_r"), TEXT("upperarm_r"), TEXT("forearm_r"), TEXT("hand_r"), TEXT("finger_r"),
		TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("toe_l"),
		TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"), TEXT("toe_r"),
		TEXT("tail_01"), TEXT("tail_02"), TEXT("tail_03"), TEXT("tail_04"), TEXT("tail_05"), TEXT("tail_06"), TEXT("tail_07"), TEXT("tail_08"),
	};

	// Leg chains: FL, FR (scapula, humerus, forearm, metacarpus, toes) and HL, HR (-, femur, tibia, metatarsus, toes).
	const int32 GLegBones[4][5] = {
		{ CatBone::ScapL, CatBone::UArmL, CatBone::FArmL, CatBone::HandL, CatBone::FingL },
		{ CatBone::ScapR, CatBone::UArmR, CatBone::FArmR, CatBone::HandR, CatBone::FingR },
		{ INDEX_NONE, CatBone::ThighL, CatBone::CalfL, CatBone::FootL, CatBone::ToeL },
		{ INDEX_NONE, CatBone::ThighR, CatBone::CalfR, CatBone::FootR, CatBone::ToeR },
	};

	float Frac(float X) { return X - FMath::FloorToFloat(X); }

	float Sat(float X) { return FMath::Clamp(X, 0.f, 1.f); }

	/**
	 * Gait timing (unit-scale cm and s). The stance sweep (stride x duty) is what the legs must reach while a paw is
	 * planted; the front legs are nearly straight at rest, so it is budgeted at ~23 cm (about +-11.5 around the
	 * shoulder, with the chest carried lower and the shoulder blades rolling). Within that budget the stride is as
	 * long as possible so the cadence stays calm (~2.3 Hz walking, ~3.6 Hz trotting) and each swing lasts long enough
	 * (~0.2 s) to read as a step, not a flicker.
	 */
	float StrideFor(float SpeedN) { return FMath::Clamp(24.f + 0.18f * SpeedN, 26.f, 66.f); }
	float DutyFor(float SpeedN) { return FMath::Lerp(FMath::Lerp(0.6f, 0.5f, Sat((SpeedN - 40.f) / 70.f)), 0.36f, Sat((SpeedN - 120.f) / 110.f)); }
	FQuat PY(float Pitch, float Yaw) { return FRotator(Pitch, Yaw, 0.f).Quaternion(); }
	/** Rotation whose X is X and whose Y is as close to Y as possible. Degenerate input (zero / NaN X, X parallel to Y),
	 *  which a single frame of coincident IK points can produce, falls back to a valid frame instead of an ensure. */
	FQuat Frame(const FVector& X, const FVector& Y)
	{
		FVector XN = X.GetSafeNormal();
		if (XN.IsNearlyZero() || XN.ContainsNaN())
		{
			XN = FVector::ForwardVector;
		}
		FVector YN = Y.ContainsNaN() ? FVector::ZeroVector : Y - XN * FVector::DotProduct(Y, XN);
		if (!YN.Normalize())
		{
			YN = FVector::CrossProduct(FVector::UpVector, XN);
			if (!YN.Normalize())
			{
				YN = FVector::RightVector;
			}
		}
		return FRotationMatrix::MakeFromXY(XN, YN).ToQuat();
	}

	/** Analytic two-bone IK. Pole is the direction the middle joint should bend toward. */
	void SolveTwoBone(const FVector& A, const FVector& T, float L1, float L2, const FVector& Pole, FVector& OutMid, FVector& OutEnd)
	{
		const FVector D = T - A;
		float Dist = D.Size();
		const FVector Dir = Dist > KINDA_SMALL_NUMBER ? D / Dist : FVector::DownVector;
		Dist = FMath::Clamp(Dist, FMath::Abs(L1 - L2) + 0.05f, (L1 + L2) * 0.999f);
		const float CosA = FMath::Clamp((L1 * L1 + Dist * Dist - L2 * L2) / (2.f * L1 * Dist), -1.f, 1.f);
		FVector P = Pole - Dir * FVector::DotProduct(Pole, Dir);
		if (!P.Normalize())
		{
			P = FVector::ForwardVector;
		}
		OutMid = A + Dir * (L1 * CosA) + P * (L1 * FMath::Sqrt(FMath::Max(0.f, 1.f - CosA * CosA)));
		OutEnd = A + Dir * Dist;
	}

	FVector TailDir(float PitchDeg, float YawDeg)
	{
		const float T = FMath::DegreesToRadians(PitchDeg);
		const float F = FMath::DegreesToRadians(YawDeg);
		return FVector(-FMath::Cos(T) * FMath::Cos(F), FMath::Cos(T) * FMath::Sin(F), FMath::Sin(T));
	}
}

// =====================================================================================================================
// Pose params
// =====================================================================================================================

void FCatPoseParams::Zero()
{
	PelvisOffset = FVector::ZeroVector;
	BodyPitch = BodyRoll = NeckPitch = HeadPitch = HeadYaw = HeadRoll = EarBack = EarOut = Lid = 0.f;
	for (int32 i = 0; i < 4; ++i)
	{
		SpineYaw[i] = SpinePitch[i] = Meta[i] = Curl[i] = 0.f;
		Ball[i] = FVector::ZeroVector;
	}
	Scap[0] = Scap[1] = 0.f;
	for (int32 i = 0; i < 8; ++i)
	{
		TailPitch[i] = TailYaw[i] = 0.f;
	}
}

void FCatPoseParams::Add(const FCatPoseParams& O, float W)
{
	if (W <= 0.f)
	{
		return;
	}
	PelvisOffset += O.PelvisOffset * W;
	BodyPitch += O.BodyPitch * W;
	BodyRoll += O.BodyRoll * W;
	NeckPitch += O.NeckPitch * W;
	HeadPitch += O.HeadPitch * W;
	HeadYaw += O.HeadYaw * W;
	HeadRoll += O.HeadRoll * W;
	EarBack += O.EarBack * W;
	EarOut += O.EarOut * W;
	Lid += O.Lid * W;
	for (int32 i = 0; i < 4; ++i)
	{
		SpineYaw[i] += O.SpineYaw[i] * W;
		SpinePitch[i] += O.SpinePitch[i] * W;
		Meta[i] += O.Meta[i] * W;
		Curl[i] += O.Curl[i] * W;
		Ball[i] += O.Ball[i] * W;
	}
	Scap[0] += O.Scap[0] * W;
	Scap[1] += O.Scap[1] * W;
	for (int32 i = 0; i < 8; ++i)
	{
		TailPitch[i] += O.TailPitch[i] * W;
		TailYaw[i] += O.TailYaw[i] * W;
	}
}

// =====================================================================================================================
// Setup
// =====================================================================================================================

UShadeCatRig::UShadeCatRig()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	for (int32& I : BoneIndex) { I = INDEX_NONE; }
	for (int32 b = 0; b < CatBone::Count; ++b)
	{
		RestPos[b] = FVector::ZeroVector;
		RestRot[b] = FQuat::Identity;
		RestDir[b] = FVector::ForwardVector;
		BoneLen[b] = 1.f;
	}
}

void UShadeCatRig::Init(UPoseableMeshComponent* InMesh)
{
	using namespace CatBone;
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
	for (int32 b = 0; b < Count; ++b)
	{
		BoneIndex[b] = Ref.FindBoneIndex(FName(GBoneNames[b]));
		if (BoneIndex[b] == INDEX_NONE && b != Root)
		{
			++Missing;
		}
	}
	if (Missing > 0)
	{
		UE_LOG(LogShade, Warning, TEXT("CatRig: %d bones missing from skeleton (%d total) - is SK_Cat up to date?"), Missing, N);
		return;
	}

	auto CompLoc = [this](int32 b) { return RefComp[BoneIndex[b]].GetLocation(); };
	AxU = FVector::UpVector;
	FVector F = CompLoc(Head) - CompLoc(Pelvis);
	F.Z = 0.f;
	AxF = F.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	AxR = FVector::CrossProduct(AxU, AxF);
	FacingYaw = -FMath::RadiansToDegrees(FMath::Atan2(AxF.Y, AxF.X));
	RigToComp = Frame(AxF, AxR);
	// The authored cat measures 34 cm (horizontal) from the pelvis bone to the head bone.
	const float Span = F.Size();
	UnitScale = Span > 1e-5f ? FMath::Clamp(34.f / Span, 0.001f, 1000.f) : 1.f;
	if (FMath::IsNearlyEqual(UnitScale, 1.f, 0.2f))
	{
		UnitScale = 1.f;
	}

	const FQuat CompToRig = RigToComp.Inverse();
	for (int32 b = 0; b < Count; ++b)
	{
		const FTransform& T = RefComp[BoneIndex[b] == INDEX_NONE ? 0 : BoneIndex[b]];
		RestPos[b] = CompToRig.RotateVector(T.GetLocation()) * UnitScale;
		RestRot[b] = CompToRig * T.GetRotation();
	}
	// Directions / lengths from each bone to the joint it drives.
	auto Link = [this](int32 B, int32 Child)
	{
		const FVector D = RestPos[Child] - RestPos[B];
		BoneLen[B] = FMath::Max(D.Size(), 0.1f);
		RestDir[B] = D.GetSafeNormal();
	};
	Link(Pelvis, Spine1); Link(Spine1, Spine2); Link(Spine2, Spine3); Link(Spine3, Neck); Link(Neck, Head);
	for (int32 l = 0; l < 4; ++l)
	{
		const int32* C = GLegBones[l];
		if (C[0] != INDEX_NONE) { Link(C[0], C[1]); }
		Link(C[1], C[2]); Link(C[2], C[3]); Link(C[3], C[4]);
	}
	for (int32 t = Tail1; t < Tail8; ++t) { Link(t, t + 1); }
	// End bones point along the same local axis as the others (Blender bones export along one axis).
	const FVector AlongLocal = RestRot[Neck].Inverse().RotateVector(RestDir[Neck]);
	FVector Axis = FVector::ForwardVector;
	{
		const FVector A = AlongLocal.GetAbs();
		Axis = (A.X >= A.Y && A.X >= A.Z) ? FVector(FMath::Sign(AlongLocal.X), 0, 0) : (A.Y >= A.Z ? FVector(0, FMath::Sign(AlongLocal.Y), 0) : FVector(0, 0, FMath::Sign(AlongLocal.Z)));
	}
	for (int32 b : { (int32)Head, (int32)EarL, (int32)EarR, (int32)FingL, (int32)FingR, (int32)ToeL, (int32)ToeR, (int32)Tail8, (int32)Root })
	{
		RestDir[b] = RestRot[b].RotateVector(Axis);
	}
	BoneLen[FingL] = BoneLen[FingR] = BoneLen[ToeL] = BoneLen[ToeR] = 2.45f;
	BoneLen[Head] = 8.1f;
	BoneLen[EarL] = BoneLen[EarR] = 4.6f;
	BoneLen[Tail8] = 2.45f;

	for (int32 l = 0; l < 4; ++l)
	{
		FCatLeg& L = Legs[l];
		L.bFront = l < 2;
		L.Home = RestPos[GLegBones[l][4]];
		L.Side = FMath::Sign(L.Home.Y);
		L.bInit = false;
	}
	bTailInit = false;
	CompPose.SetNum(N);
	bReady = true;
	UE_LOG(LogShade, Log, TEXT("CatRig ready: %d bones, forward (%.2f %.2f %.2f), facing yaw %.1f, unit scale %.3f, bone axis %s, FL home %s"),
		N, AxF.X, AxF.Y, AxF.Z, FacingYaw, UnitScale, *Axis.ToCompactString(), *Legs[0].Home.ToCompactString());
}

void UShadeCatRig::SetPose(ECatPose Pose, float BlendTime)
{
	TargetPose = Pose;
	BlendRate = 1.f / FMath::Max(BlendTime, 0.05f);
	const int32 Fam = FamilyOf(Pose);
	if (Fam == FamLoco)
	{
		PushRate = BlendRate;  // pushing is a layer on locomotion, eased in and out on its own
	}
	if (Fam == TransTo)
	{
		return;
	}
	if (TransS < 1.f)
	{
		// Changed its mind halfway: carry on from the body as it is now, not from either end.
		Snap = LastP;
		SnapFam = FamW[FamSit] >= FamW[FamLoco] && FamW[FamSit] >= FamW[FamSleep] ? FamSit : (FamW[FamSleep] > FamW[FamLoco] ? FamSleep : FamLoco);
		TransFrom = FamSnap;
	}
	else
	{
		TransFrom = TransTo;
	}
	TransTo = Fam;
	TransS = 0.f;
	TransDur = FMath::Max(BlendTime, 0.05f);
}

void UShadeCatRig::Land(float Strength)
{
	LandStrength = FMath::Clamp(Strength, 0.f, 1.5f);
	LandComp.Velocity += 55.f * LandStrength;
}

void UShadeCatRig::ResetDynamics()
{
	for (FCatLeg& L : Legs)
	{
		L.bInit = false;
	}
	bTailInit = false;
}

float UShadeCatRig::TraceGround(const FVector& World, float Fallback, bool* bOutHit) const
{
	if (bOutHit)
	{
		*bOutHit = false;
	}
	UWorld* W = GetWorld();
	if (!W)
	{
		return Fallback;
	}
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(CatPaw), false, GetOwner());
	const float Up = 32.f * MeshScale;
	if (W->LineTraceSingleByChannel(Hit, World + FVector(0, 0, Up), World - FVector(0, 0, 55.f * MeshScale), ECC_Visibility, Q))
	{
		if (bOutHit)
		{
			*bOutHit = true;
		}
		return Hit.ImpactPoint.Z;
	}
	return Fallback;
}

FVector UShadeCatRig::Foothold(const FVector& Want, const FCatLeg& L, float Fallback, float* OutPull) const
{
	// Ground under the spot (a lower step within the trace counts: the body pitches down to it, as on stairs). Over thin
	// air - past the end of a window box, a sill, a wall top - search back along the body for the edge and stand on it.
	if (OutPull)
	{
		*OutPull = 0.f;
	}
	bool bHit = false;
	const float Z = TraceGround(Want, Fallback, &bHit);
	if (bHit)
	{
		return FVector(Want.X, Want.Y, Z);
	}
	const float M = MeshScale;
	const FVector Back = ToWorld(FVector((L.Home.X + (L.bFront ? -32.f : 32.f)) * M, L.Home.Y * M, 0.f));
	for (int32 k = 1; k <= 10; ++k)
	{
		FVector P = FMath::Lerp(Want, Back, k / 10.f);
		P.Z = Want.Z;
		bool bK = false;
		const float Zk = TraceGround(P, Fallback, &bK);
		if (bK)
		{
			FVector In = FMath::Lerp(Want, Back, FMath::Min(1.f, (k + 0.4f) / 10.f));   // a little in from the edge
			if (OutPull)
			{
				*OutPull = FVector::Dist2D(Want, In);
			}
			return FVector(In.X, In.Y, Zk);
		}
	}
	return FVector(Want.X, Want.Y, Fallback);
}

FQuat UShadeCatRig::Aim(int32 Bone, const FVector& Dir, const FVector& SideHint) const
{
	const FQuat Delta = Frame(Dir.GetSafeNormal(), SideHint) * Frame(RestDir[Bone], FVector::RightVector).Inverse();
	return Delta * RestRot[Bone];
}

// =====================================================================================================================
// Tick
// =====================================================================================================================

void UShadeCatRig::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bReady || !Mesh.IsValid())
	{
		return;
	}
	const float Dt = FMath::Clamp(DeltaTime, 1e-4f, 0.05f);
	Time += Dt;
	const FTransform CT = Mesh->GetComponentTransform();
	RigToWorld = FTransform(CT.GetRotation() * RigToComp, CT.GetLocation(), FVector::OneVector);

	using namespace CatBone;
	// Where the current pose change has got to, and how much of each family is in the body.
	TransS = FMath::Min(1.f, TransS + Dt / TransDur);
	PushAmt = FMath::FInterpConstantTo(PushAmt, TargetPose == ECatPose::Push ? 1.f : 0.f, Dt, PushRate);
	{
		const float WTo = ShadeEase::Smooth(TransS);
		FamW[FamLoco] = FamW[FamSit] = FamW[FamSleep] = 0.f;
		FamW[TransTo] += WTo;
		FamW[TransFrom == FamSnap ? SnapFam : TransFrom] += 1.f - WTo;
	}
	SeatedW = 1.f - FamW[FamLoco];
	PoseW[(int32)ECatPose::Locomote] = FamW[FamLoco] * (1.f - PushAmt);
	PoseW[(int32)ECatPose::Push] = FamW[FamLoco] * PushAmt;
	PoseW[(int32)ECatPose::Sit] = FamW[FamSit];
	PoseW[(int32)ECatPose::Sleep] = FamW[FamSleep];
	const float SleepW = FamW[FamSleep];
	// Seated, and until the body is mostly up again, the gait doesn't tidy the paws back under the body.
	bHoldGait = TransTo != FamLoco || TransS < 0.65f;
	// The ground under the cat in rig space: sitting and lying rest on it, not on the rig origin (a nap cat placed a few
	// cm above its bench floated there; the walking capsule hovers a couple of cm).
	if (UWorld* W = GetWorld())
	{
		// (starts just above the paws, so a bench or a table the cat sits under is not taken for the floor)
		const FVector C = RigToWorld.GetLocation();
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(CatGround), false, GetOwner());
		float Want = 0.f;
		if (W->LineTraceSingleByChannel(Hit, C + FVector(0, 0, 8.f * MeshScale), C - FVector(0, 0, 20.f * MeshScale), ECC_Visibility, Q))
		{
			Want = FMath::Clamp(Hit.ImpactPoint.Z - C.Z, -12.f * MeshScale, 6.f * MeshScale);
		}
		GroundRig = FMath::Lerp(GroundRig, Want, 1.f - FMath::Exp(-10.f * Dt));
	}

	FCatPoseParams Loco;
	ComputeLocomotion(Dt, Loco);
	if (PushAmt > 0.f)
	{
		// Shoving the cart with the head and shoulders: low, neck stretched forward, ears back, tail down.
		FCatPoseParams A;
		A.PelvisOffset = FVector(1.5f, 0.f, -3.f) * MeshScale;
		A.SpinePitch[2] = -4.f;
		A.SpinePitch[3] = -6.f;
		A.NeckPitch = -16.f;
		A.HeadPitch = -14.f;
		A.EarBack = 14.f;
		for (int32 i = 0; i < 8; ++i) { A.TailPitch[i] = -14.f; }
		Loco.Add(A, PushAmt);
	}
	UpdateIdleLife(Dt);
	FCatPoseParams P;
	if (TransS >= 1.f)
	{
		FamilyParams(TransTo, Loco, P);
	}
	else
	{
		FCatPoseParams From, To;
		FamilyParams(TransFrom, Loco, From);
		FamilyParams(TransTo, Loco, To);
		BlendShaped(From, To, P);
	}
	LastP = P;
	if (TransTo != FamLoco && TransS >= 1.f && bGrounded)
	{
		// Sitting or lying, the gait's paws are wherever the pose has put them: getting up then starts from there (the
		// body rises over planted paws and the hind paws step back under the hips) instead of sliding them home.
		for (int32 l = 0; l < 4; ++l)
		{
			FCatLeg& L = Legs[l];
			L.PlantWorld = L.BallWorld = ToWorld(P.Ball[l]);
			L.bPlanted = true;
			L.SwingT = 1.f;
			L.bInit = true;
			L.Pull = 0.f;
		}
	}
	ApplyAction(P);

	// Breathing (slower and deeper asleep; the rate eases, the phase never jumps), yawn + stretch.
	BreathPhase += Dt * FMath::Lerp(2.4f, 1.5f, SleepW);
	Breath = FMath::Sin(BreathPhase);
	P.SpinePitch[2] += Breath * (0.5f + 1.1f * SleepW);
	P.SpinePitch[3] -= Breath * 0.4f * (0.4f + SleepW);
	YawnTime += Dt;
	if (YawnTime < 2.8f)
	{
		const float Y = ShadeEase::Bump(YawnTime, 0.f, 2.8f);
		P.HeadPitch += 32.f * Y;
		P.NeckPitch += 12.f * Y;
		P.HeadRoll += 6.f * Y;
		P.EarBack += 30.f * Y;
		P.EarOut += 14.f * Y;
		P.SpinePitch[1] -= 5.f * Y;
	}
	// No blinks, squints or closed lids: closed lids read as empty sockets on the eye material, so the eyes stay open.
	EyeLid = 0.f;

	// Ear twitches: a quick flick back and out, sprung.
	EarTimer -= Dt;
	if (EarTimer <= 0.f)
	{
		EarTimer = FMath::FRandRange(1.5f, 4.5f);
		EarTwitch[FMath::RandRange(0, 1)].Velocity += 900.f;
	}
	EarTwitch[0].Update(0.f, 420.f, 18.f, Dt);
	EarTwitch[1].Update(0.f, 420.f, 18.f, Dt);

	P.PelvisOffset.X += ClearX;
	Solve(P);
	UpdateClearance();
	UpdateTail(Dt, P, TailRootRig, TailPelvisDelta);
	Apply();
}

void UShadeCatRig::UpdateClearance()
{
	using namespace CatBone;
	const float M = MeshScale;
	// Not while the head is meant to touch (shoving the cart) or the paws are working something.
	const float Allow = (1.f - Sat(ActW)) * (1.f - Sat(PushAmt * 2.f));
	float Pen = 0.f;
	UWorld* W = GetWorld();
	if (W && Allow > 0.01f && Out[Head].bSet && Out[Spine1].bSet)
	{
		const FVector NoseDir = (Out[Head].Rot * RestRot[Head].Inverse()).RotateVector(RestDir[Head]);
		const FVector From = ToWorld(Out[Spine1].Pos + FVector(0.f, 0.f, 2.f * M));
		const FVector Nose = ToWorld(Out[Head].Pos + NoseDir * (BoneLen[Head] * 1.05f * M));
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(CatNose), false, GetOwner());
		if (W->LineTraceSingleByChannel(Hit, From, Nose, ECC_Visibility, Q) && !Hit.bStartPenetrating)
		{
			const FVector BodyFwd = RigToWorld.TransformVectorNoScale(FVector::ForwardVector);
			Pen = FMath::Max(0.f, FVector::DotProduct(Nose - Hit.ImpactPoint, BodyFwd));
		}
	}
	// Draw back by what is in the way (plus a hair), ease forward again when clear; like LowerFix it measures the pose that
	// already includes the shift, so it settles instead of oscillating.
	const float Want = Pen > 0.2f ? ClearX - Pen - 0.6f * M : FMath::Min(0.f, ClearX + 0.6f * M);
	ClearX = FMath::Lerp(ClearX, FMath::Clamp(Want, -16.f * M, 0.f) * Allow, 0.25f);
}

// =====================================================================================================================
// Locomotion
// =====================================================================================================================

void UShadeCatRig::UpdateGait(float Dt, float SpeedN, const FVector& VelRig, const FVector& VelWorld)
{
	const float M = MeshScale;
	const float StrideN = StrideFor(SpeedN);
	Freq = SpeedN > 6.f ? SpeedN / StrideN : 0.f;
	Duty = DutyFor(SpeedN);
	if (!bTrot && SpeedN > 165.f) { bTrot = true; }
	else if (bTrot && SpeedN < 140.f) { bTrot = false; }
	// Lateral-sequence walk: HL, FL, HR, FR. Trot: diagonal pairs.
	static const float WalkOff[4] = { 0.25f, 0.75f, 0.0f, 0.5f };
	static const float TrotOff[4] = { 0.0f, 0.5f, 0.5f, 0.0f };
	const float* Off = bTrot ? TrotOff : WalkOff;
	Phase = Frac(Phase + Freq * Dt);
	StepCooldown -= Dt;

	const float GroundFallback = RigToWorld.GetLocation().Z;
	for (int32 l = 0; l < 4; ++l)
	{
		FCatLeg& L = Legs[l];
		const FVector HomeW = ToWorld(L.Home * M + FVector(ClearX, 0.f, 0.f));   // (drawn back with the body, see UpdateClearance)
		if (!L.bInit || FVector::Dist2D(L.BallWorld, HomeW) > 70.f * M)
		{
			L.PlantWorld = L.BallWorld = Foothold(HomeW, L, GroundFallback, &L.Pull) + FVector(0.f, 0.f, L.Home.Z * M);
			L.bPlanted = true;
			L.SwingT = 1.f;
			L.bInit = true;
		}
		if (TreadmillSpeed > 0.f && L.bPlanted)
		{
			L.PlantWorld -= VelWorld * Dt;
			L.BallWorld = L.PlantWorld;
		}
	}

	AirTime = bGrounded ? 0.f : AirTime + Dt;
	if (!bGrounded)
	{
		for (FCatLeg& L : Legs) { L.bPlanted = false; L.SwingT = 1.f; L.Pull = 0.f; }
		bWasGrounded = false;
		return;
	}
	const float Stride = StrideN * M;
	auto StartSwing = [&](FCatLeg& L, float Dur, bool bSettle)
	{
		L.bPlanted = false;
		L.SwingFrom = L.BallWorld;
		L.SwingT = 0.f;
		L.SwingDur = Dur;
		L.bSettleStep = bSettle;
		const float Base = L.bFront ? 3.9f : 3.3f;
		L.SwingLift = (bSettle ? 2.2f : Base * FMath::Lerp(0.7f, 1.35f, Sat((SpeedN - 40.f) / 220.f))) * M;
	};

	if (!bWasGrounded)
	{
		// Touchdown, forepaws first: they plant where the air pose reached them and take the weight (the nose dips);
		// the hind paws, still folded under the belly, come down a beat later - longer after a bigger drop - and plant
		// under the hips.
		const float HindDelay = 0.09f + 0.07f * Sat(LandStrength);
		for (int32 l = 0; l < 4; ++l)
		{
			FCatLeg& L = Legs[l];
			if (L.bFront)
			{
				L.PlantWorld = L.BallWorld = Foothold(L.BallWorld, L, GroundFallback, &L.Pull) + FVector(0.f, 0.f, L.Home.Z * M);
				L.bPlanted = true;
				L.SwingT = 1.f;
				if (OnFootPlant) { OnFootPlant(l); }
			}
			else
			{
				StartSwing(L, HindDelay, true);
				L.SwingLift = 0.8f * M;
			}
		}
		LandPitchS.Velocity -= 60.f + 90.f * Sat(LandStrength);
		bWasGrounded = true;
	}

	int32 Swinging = 0;
	for (const FCatLeg& L : Legs) { Swinging += L.bPlanted ? 0 : 1; }
	for (int32 l = 0; l < 4; ++l)
	{
		FCatLeg& L = Legs[l];
		const float Ph = Frac(Phase + Off[l]);
		if (Freq > 0.f && L.bPlanted)
		{
			const bool bCross = (L.PrevPhase < Duty && Ph >= Duty) || (L.PrevPhase > Ph && Ph >= Duty);
			// A paw left far behind (turning, accelerating) steps early.
			// (a paw held back on a ledge's edge doesn't count as left behind: stepping again would land it there again)
			const FVector Rel = ToRig(L.PlantWorld) - L.Home * M - FVector(ClearX, 0.f, 0.f);
			const bool bLate = Rel.X + (L.bFront ? L.Pull : 0.f) < -0.62f * Stride || FVector2D(Rel.X, Rel.Y).Size() - L.Pull > 0.85f * Stride;
			if (bCross || bLate)
			{
				StartSwing(L, FMath::Clamp((1.f - Duty) / Freq, 0.15f, 0.5f), false);
				++Swinging;
			}
		}
		L.PrevPhase = Ph;
	}
	// Standing (or barely moving): re-step the paw that drifted most from under the body, one at a time.
	if (SpeedN < 20.f && Swinging == 0 && StepCooldown <= 0.f && !bHoldGait)
	{
		int32 Best = INDEX_NONE;
		float BestD = 3.8f * M;
		for (int32 l = 0; l < 4; ++l)
		{
			const FVector Rel = ToRig(Legs[l].PlantWorld) - Legs[l].Home * M - FVector(ClearX, 0.f, 0.f);
			const float D = FVector2D(Rel.X, Rel.Y).Size() - Legs[l].Pull;
			if (D > BestD) { BestD = D; Best = l; }
		}
		if (Best != INDEX_NONE)
		{
			StartSwing(Legs[Best], 0.21f, true);
			StepCooldown = 0.06f;
		}
	}

	for (int32 l = 0; l < 4; ++l)
	{
		FCatLeg& L = Legs[l];
		if (L.bPlanted)
		{
			L.BallWorld = L.PlantWorld;
		}
		else
		{
			L.SwingT += Dt / FMath::Max(L.SwingDur, 0.05f);
			const float T = Sat(L.SwingT);
			const float Remaining = (1.f - T) * L.SwingDur;
			FVector Target = ToWorld(L.Home * M + FVector(ClearX, 0.f, 0.f));
			if (!L.bSettleStep && Freq > 0.f)
			{
				// Land ahead of the hip/shoulder so the paw passes under it at mid-stance.
				Target += VelWorld * (Remaining + 0.5f * Duty / Freq);
			}
			float Pull = 0.f;
			Target = Foothold(Target, L, GroundFallback, &Pull) + FVector(0.f, 0.f, L.Home.Z * M);
			L.SwingTo = Target;
			const float E = ShadeEase::Smooth(T);
			FVector Pos = FMath::Lerp(L.SwingFrom, L.SwingTo, E);
			Pos.Z += L.SwingLift * FMath::Sin(PI * FMath::Pow(T, 0.8f));
			L.BallWorld = Pos;
			if (L.SwingT >= 1.f)
			{
				L.bPlanted = true;
				L.Pull = Pull;
				L.PlantWorld = L.BallWorld = L.SwingTo;
				if (OnFootPlant) { OnFootPlant(l); }
			}
		}
		const FVector G = ToRig(L.bPlanted ? L.PlantWorld : L.SwingTo);
		L.GroundZ = G.Z - L.Home.Z * M;
	}
}

void UShadeCatRig::ComputeLocomotion(float Dt, FCatPoseParams& P)
{
	using namespace CatBone;
	P.Zero();
	const float M = MeshScale;
	FVector VelW = Velocity;
	if (TreadmillSpeed > 0.f)
	{
		VelW = RigToWorld.TransformVectorNoScale(FVector(TreadmillSpeed, 0.f, 0.f));
	}
	FVector VelRig = RigToWorld.InverseTransformVectorNoScale(VelW);
	const float SpeedN = VelRig.Size2D() / M;
	SmoothSpeed = ShadeEase::Approach(SmoothSpeed, SpeedN, 6.f, Dt);
	UpdateGait(Dt, SpeedN, VelRig, VelW);

	const float Moving = Sat(SmoothSpeed / 60.f);
	const float Sprint = Sat((SmoothSpeed - 140.f) / 120.f);
	const float StrideCm = StrideFor(SpeedN) * M;
	const float Cyc = Phase * 2.f * PI;

	// Heights follow the paws: the pelvis rides the hind feet, the chest the front feet (stairs pitch the body).
	const float Hind = 0.5f * (Legs[2].GroundZ + Legs[3].GroundZ);
	const float Front = 0.5f * (Legs[0].GroundZ + Legs[1].GroundZ);
	HindZ.Update(Hind, 170.f, 24.f, Dt);
	FrontZ.Update(Front, 170.f, 24.f, Dt);
	AirS.Update(bGrounded ? 0.f : 1.f, 320.f, 34.f, Dt);
	const float Air = Sat(AirS.Value);
	const float Up = Sat(Velocity.Z / 350.f);
	const float Down = Sat(-Velocity.Z / 450.f);

	const FVector Acc = (VelRig - PrevVelRig) / Dt;
	PrevVelRig = VelRig;
	AccelPitch.Update(FMath::Clamp(-Acc.X / 110.f, -4.f, 4.f) * (1.f - Air), 90.f, 15.f, Dt);
	Bend.Update(FMath::Clamp(TurnRate * 0.055f, -24.f, 24.f), 80.f, 16.f, Dt);
	CrouchS.Update(Crouch, 420.f, 34.f, Dt);
	LaunchS.Update(bGrounded ? Launch : 0.f, 520.f, 40.f, Dt);
	LandPitchS.Update(0.f, 140.f, 15.f, Dt);
	const float Lift = FMath::Max(LaunchS.Value, 0.f);
	LandComp.Update(0.f, 150.f, 13.f, Dt);
	const float Comp = FMath::Max(LandComp.Value, 0.f) * 0.11f;   // cm-ish, sprung

	const float Bob = (0.35f + 0.8f * Sprint) * M * Moving * (0.5f - 0.5f * FMath::Cos(2.f * Cyc));
	// On the move a cat carries itself a little lower than it stands, the chest lowest. Without that the nearly
	// straight front legs had no reach left for the stride: they locked straight and the planted paws skated.
	const float Carry = Moving * (1.f - Air);
	const float HipDrop = (1.2f + 0.9f * Sprint) * M * Carry;
	const float ChestDrop = (2.6f + 1.2f * Sprint) * M * Carry;
	// Hind paws still coming down after a forepaws-first landing hold the rump up a moment.
	float HindLanding = 0.f;
	for (int32 l = 2; l < 4; ++l) { HindLanding += (!Legs[l].bPlanted && bGrounded && Legs[l].bSettleStep) ? 0.5f : 0.f; }
	// Paws that could only find footing back on a ledge's edge (the end of a window box): the body draws back over them
	// instead of leaning out over thin air (the capsule may perch a few cm past the edge).
	const float FrontPull = FMath::Max(Legs[0].Pull, Legs[1].Pull);
	const float HindPull = FMath::Max(Legs[2].Pull, Legs[3].Pull);
	LedgeS.Update(bGrounded ? FMath::Clamp(0.8f * (HindPull - FrontPull), -11.f * M, 11.f * M) : 0.f, 140.f, 24.f, Dt);
	P.PelvisOffset = FVector(-1.2f * CrouchS.Value * M - 1.5f * Lift * M + LedgeS.Value, 0.f,
		HindZ.Value - Bob - HipDrop - CrouchS.Value * 5.5f * M - Lift * 1.2f * M - Comp * M + LowerFix.Value + HindLanding * 1.6f * M);
	// In the air the body follows the arc: nose up and stretched on the rise, nose down reaching for the landing.
	P.BodyPitch = FMath::RadiansToDegrees(FMath::Atan2(FrontZ.Value - HindZ.Value - (ChestDrop - HipDrop), 27.f * M)) + AccelPitch.Value
		+ Air * (20.f * Up - 18.f * Down) - CrouchS.Value * 4.f + 26.f * Lift + LandPitchS.Value;
	// A slight weight shift from side to side with the steps.
	P.BodyRoll = FMath::Sin(Cyc) * 1.1f * Carry * (1.f - Sprint);

	// Spine: bends into turns, wiggles with the walk, flexes with the trot, crouches, stretches in the air.
	const float Wig = FMath::Sin(Cyc) * 2.2f * Moving * (1.f - Sprint);
	P.SpineYaw[0] = -0.3f * Bend.Value - Wig * 0.6f;
	P.SpineYaw[1] = 0.2f * Bend.Value + Wig;
	P.SpineYaw[2] = 0.35f * Bend.Value - Wig * 0.4f;
	P.SpineYaw[3] = 0.35f * Bend.Value;
	const float Flex = FMath::Sin(2.f * Cyc) * 2.4f * Sprint;
	P.SpinePitch[1] = Flex + 5.f * CrouchS.Value - 4.f * Air * Up + 3.f * Comp + 4.f * Lift;
	P.SpinePitch[2] = -Flex * 0.5f + 3.f * CrouchS.Value;
	P.SpinePitch[3] = -Flex * 0.6f - 2.f * CrouchS.Value;
	P.NeckPitch = -5.f * Moving - 6.f * Sprint;

	// Gaze: look at points of interest, into turns when walking, glancing around when idle; head stays level.
	float WantYaw = Bend.Value * 0.9f;
	float WantPitch = -5.f * Moving - 5.f * CrouchS.Value;
	if (bHasLook)
	{
		const FVector L = ToRig(LookAtWorld) - (RP(Head) + P.PelvisOffset);
		WantYaw = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(L.Y, FMath::Max(L.X, 1.f))), -70.f, 70.f);
		WantPitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(L.Z, FVector2D(L.X, L.Y).Size())), -38.f, 40.f);
	}
	else if (Moving < 0.3f)
	{
		LookWander += Dt;
		WantYaw += 16.f * FMath::Sin(LookWander * 0.37f) * FMath::Sin(LookWander * 0.11f + 1.f);
		WantPitch += 5.f * FMath::Sin(LookWander * 0.23f);
	}
	HeadYawS.Update(WantYaw, 70.f, 15.f, Dt);
	HeadPitchS.Update(WantPitch, 70.f, 15.f, Dt);
	P.HeadYaw = HeadYawS.Value;
	P.HeadPitch = HeadPitchS.Value;
	P.HeadRoll = FMath::Sin(Cyc) * 1.2f * Moving;

	// Paws.
	for (int32 l = 0; l < 4; ++l)
	{
		const FCatLeg& L = Legs[l];
		P.Ball[l] = ToRig(L.BallWorld);
		const float Rel = FMath::Clamp((P.Ball[l].X - L.Home.X * M - ClearX) / (0.5f * StrideCm), -1.4f, 1.4f);
		const bool bSwing = !L.bPlanted && bGrounded;
		const float T = Sat(L.SwingT);
		if (L.bFront)
		{
			if (!bSwing)
			{
				P.Meta[l] = FMath::Lerp(20.f, 52.f, Sat(-Rel)) * FMath::Lerp(0.5f, 1.f, Moving);
				P.Curl[l] = 0.f;
			}
			else
			{
				// The wrist folds the paw back as it lifts, then the leg reaches and the paw opens flat for landing.
				const float Fold = -46.f - 22.f * Sprint;
				P.Meta[l] = T < 0.45f ? FMath::Lerp(52.f, Fold, ShadeEase::Smooth(T / 0.45f)) : FMath::Lerp(Fold, 14.f, ShadeEase::Smooth((T - 0.45f) / 0.55f));
				P.Curl[l] = FMath::Sin(PI * T) * (40.f + 18.f * Sprint);
			}
			// Shoulder blades roll with the leg: the shoulder joint follows the paw by about a quarter of its travel
			// (this is most of the visible "cat walk" and gives the leg its reach).
			const float Dx = P.Ball[l].X - L.Home.X * M - ClearX;
			const float ScapLen = BoneLen[GLegBones[l][0]] * M;
			P.Scap[l] = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(0.27f * Dx / FMath::Max(ScapLen, 1.f), -0.42f, 0.42f)))
				+ (bSwing ? FMath::Sin(PI * T) * 5.f : 0.f);
		}
		else
		{
			if (!bSwing)
			{
				P.Meta[l] = FMath::Lerp(15.f, -30.f, Sat(-Rel)) * FMath::Lerp(0.6f, 1.f, Moving);
				P.Curl[l] = 0.f;
			}
			else
			{
				P.Meta[l] = T < 0.4f ? FMath::Lerp(-30.f, -64.f, ShadeEase::Smooth(T / 0.4f)) : FMath::Lerp(-64.f, 16.f, ShadeEase::Smooth((T - 0.4f) / 0.6f));
				P.Curl[l] = FMath::Sin(PI * T) * 50.f;
			}
		}
	}
	if (Lift > 0.001f && bGrounded)
	{
		// The spring starts at the front: the forepaws come up off the ground, curled, while the hind legs stay coiled.
		for (int32 l = 0; l < 2; ++l)
		{
			const FCatLeg& L = Legs[l];
			FVector A = FVector(L.Home.X + 4.f, L.Home.Y, 7.f) * M + FVector(ClearX, 0.f, 0.f);
			A.Z += P.PelvisOffset.Z;
			P.Ball[l] = FMath::Lerp(P.Ball[l], A, Lift);
			P.Meta[l] = FMath::Lerp(P.Meta[l], -45.f, Lift);
			P.Curl[l] = FMath::Lerp(P.Curl[l], 45.f, Lift);
		}
	}
	if (Air > 0.001f)
	{
		// A real leap: forelegs up and reaching first while the hind legs drive off straight behind (only for the first
		// instant - then they fold); a compact tuck over the top; then the forelegs reach down for the landing while
		// the hind legs stay folded under the belly (they land a beat after the fore, see UpdateGait).
		const FVector Pel = RP(Pelvis) + P.PelvisOffset;
		const float Rise = Up;
		const float Fall = Sat(-Velocity.Z / 300.f);
		const float Drive = Sat(Velocity.Z / 250.f) * (1.f - ShadeEase::Smooth((AirTime - 0.06f) / 0.12f));
		for (int32 l = 0; l < 4; ++l)
		{
			const FCatLeg& L = Legs[l];
			FVector A;
			float MetaA, CurlA;
			if (L.bFront)
			{
				const FVector Tuck(L.Home.X + 3.f, L.Home.Y, 8.5f);
				const FVector Reach(L.Home.X + 7.5f, L.Home.Y, 11.f);
				const FVector Land(L.Home.X + 6.f, L.Home.Y, -1.5f);
				A = FMath::Lerp(FMath::Lerp(Tuck, Reach, Rise), Land, Fall) * M;
				MetaA = FMath::Lerp(FMath::Lerp(-55.f, -25.f, Rise), 18.f, Fall);
				CurlA = FMath::Lerp(FMath::Lerp(55.f, 30.f, Rise), 5.f, Fall);
			}
			else
			{
				const FVector Tuck(L.Home.X + 3.f, L.Home.Y, 7.f);
				const FVector Push(L.Home.X - 9.f, L.Home.Y, -1.f);
				const FVector Fold(L.Home.X + 4.5f, L.Home.Y, 6.f);
				A = FMath::Lerp(FMath::Lerp(Tuck, Push, Drive), Fold, Fall) * M;
				MetaA = FMath::Lerp(FMath::Lerp(-35.f, -70.f, Drive), -30.f, Fall);
				CurlA = FMath::Lerp(FMath::Lerp(35.f, 15.f, Drive), 40.f, Fall);
			}
			A.Z += Pel.Z - RP(Pelvis).Z;
			A.X += ClearX;
			P.Ball[l] = FMath::Lerp(P.Ball[l], A, Air);
			P.Meta[l] = FMath::Lerp(P.Meta[l], MetaA, Air);
			P.Curl[l] = FMath::Lerp(P.Curl[l], CurlA, Air);
		}
		Legs[0].BallWorld = ToWorld(P.Ball[0]);
		Legs[1].BallWorld = ToWorld(P.Ball[1]);
		Legs[2].BallWorld = ToWorld(P.Ball[2]);
		Legs[3].BallWorld = ToWorld(P.Ball[3]);
	}

	// Tail carriage: low and curled standing, raised question-mark walking, streaming behind at a trot.
	static const float IdleP[8] = { -24.f, -38.f, -44.f, -36.f, -20.f, -4.f, 16.f, 32.f };
	static const float WalkP[8] = { 26.f, 40.f, 50.f, 56.f, 60.f, 68.f, 84.f, 100.f };
	static const float TrotP[8] = { 12.f, 14.f, 16.f, 16.f, 17.f, 19.f, 24.f, 30.f };
	static const float AirP[8] = { 10.f, 8.f, 6.f, 4.f, 3.f, 2.f, 2.f, 4.f };
	const float Ex = FMath::Clamp(Excitement, 0.f, 1.f);
	for (int32 i = 0; i < 8; ++i)
	{
		float Pt = FMath::Lerp(IdleP[i], FMath::Lerp(WalkP[i], TrotP[i], Sprint), Moving);
		Pt += (Ex - 0.5f) * (i < 3 ? 28.f : 16.f);
		// A slow wave travelling from the root to the tip keeps a carried tail alive without whipping it.
		const float Walking = Moving * (1.f - Sprint) * (1.f - Air);
		Pt += FMath::Sin(Time * 1.1f - i * 0.5f) * (i * 0.8f) * Walking;
		Pt = FMath::Lerp(Pt, AirP[i] - 10.f * Up + 22.f * Sat(-Velocity.Z / 300.f), Air);  // streams low on the rise, lifts to balance the fall
		P.TailPitch[i] = Pt;
		const float Sway = FMath::Sin(Time * 1.3f + i * 0.5f) * (1.2f + i * 1.2f) * (1.f - 0.75f * Moving)
			+ FMath::Sin(Cyc + i * 0.4f) * 1.2f * Moving + (1.f - Moving) * (4.f + i * 3.f)
			+ FMath::Sin(Time * 1.7f - i * 0.45f) * (0.6f + i * 1.3f) * Walking;
		P.TailYaw[i] = Sway;
	}

	// Ears: alert forward, flattened when scared (burning) or bracing for a jump.
	EarBackS.Update((1.f - Ex) * 26.f + CrouchS.Value * 18.f + Air * 10.f, 110.f, 16.f, Dt);
	EarOutS.Update((1.f - Ex) * 20.f + Sprint * 6.f, 110.f, 16.f, Dt);
	P.EarBack = EarBackS.Value;
	P.EarOut = EarOutS.Value;
}

void UShadeCatRig::ComputeSit(FCatPoseParams& P) const
{
	// Upright sit: rump on the ground, chest raised on straight, near-vertical forelegs with the forepaws under the
	// shoulders, hind legs folded beside the belly with the hocks flat, tail wrapped around the front paws. The chest is
	// only carried as high as the forelegs reach (31 deg of back; at 41 the forepaws hung 4-5 cm off the ground).
	P.Zero();
	const float M = MeshScale;
	P.PelvisOffset = FVector(2.f, 0.f, -11.f) * M;
	P.BodyPitch = 20.f;
	P.SpinePitch[1] = 2.f;
	P.SpinePitch[2] = 6.f;
	P.SpinePitch[3] = 3.f;
	P.NeckPitch = -0.5f;
	P.HeadPitch = 3.f;
	// Never quite still: the weight drifts a little from side to side, the head glances round (or mostly holds whatever it
	// is looking at), now and then tilting; the ears keep the alertness of the moment.
	P.BodyRoll = 0.8f * FMath::Sin(Time * 0.29f);
	P.SpineYaw[2] = 1.5f * FMath::Sin(Time * 0.21f + 1.f);
	const float GlanceK = bHasLook ? 0.25f : 1.f;
	P.HeadYaw = FMath::Clamp(HeadYawS.Value + GlanceYawS.Value * GlanceK, -65.f, 65.f);
	P.HeadPitch += 0.7f * HeadPitchS.Value + GlancePitchS.Value * GlanceK;
	P.HeadRoll = GlanceRollS.Value * GlanceK;
	P.EarBack = 0.6f * EarBackS.Value;
	P.EarOut = 0.6f * EarOutS.Value;
	for (int32 l = 0; l < 4; ++l)
	{
		const FVector H = Legs[l].Home;
		if (Legs[l].bFront)
		{
			P.Ball[l] = FVector(H.X + 1.f, H.Y * 0.9f, H.Z) * M;
			P.Meta[l] = 10.f;
			P.Scap[l] = -6.f;
		}
		else
		{
			P.Ball[l] = FVector(H.X + 11.5f, H.Y * 1.35f, H.Z) * M;
			P.Meta[l] = 86.f;
		}
	}
	if (ShuffleT < 0.55f)
	{
		// A forepaw lifted and set down again, the weight shifted off it first.
		const float Sh = ShadeEase::Bump(ShuffleT, 0.f, 0.55f);
		const int32 l = ShufflePaw;
		P.Ball[l].Z += 1.7f * M * Sh;
		P.Ball[l].X += 0.5f * M * Sh;
		P.Meta[l] -= 30.f * Sh;
		P.Curl[l] += 25.f * Sh;
		P.BodyRoll -= Legs[l].Side * 1.6f * Sh;
	}
	// Tail down off the rump and laid along the ground up the right side, the tip curling in beside the hind paw (solved
	// in Scripts/cat_rig_check.py for this body pitch; it used to stick straight out sideways). The tip flicks and creeps.
	static const float TP[8] = { -24.f, -34.f, -40.f, -26.f, -24.f, -24.f, -24.f, -22.f };
	static const float TY[8] = { 21.f, 43.f, 80.f, 155.f, 168.f, 178.f, 191.f, 209.f };
	for (int32 i = 0; i < 8; ++i)
	{
		const float Tip = Sat((i - 3) / 4.f);
		P.TailPitch[i] = TP[i] + FMath::Abs(TailFlick.Value) * 0.35f * Sat((i - 4) / 3.f);
		P.TailYaw[i] = TY[i] + TailFlick.Value * Tip + 5.f * FMath::Sin(Time * 0.7f - i * 0.5f) * Sat((i - 4) / 3.f);
	}
	// Resting on the real ground under the cat (and drawn back with the body if its nose was against something).
	P.PelvisOffset.Z += GroundRig;
	for (int32 l = 0; l < 4; ++l)
	{
		P.Ball[l].Z += GroundRig;
		P.Ball[l].X += ClearX;
	}
}

void UShadeCatRig::ComputeSleep(FCatPoseParams& P) const
{
	// Loaf with the chin tucked: belly down, front paws folded under the chest, hind legs folded, tail around the side.
	P.Zero();
	const float M = MeshScale;
	P.PelvisOffset = FVector(1.f, 0.f, -10.8f) * M;
	P.BodyPitch = -1.f;
	P.SpineYaw[1] = 3.f;
	P.SpineYaw[2] = 5.f;
	P.SpineYaw[3] = 5.f;
	P.SpinePitch[1] = -2.f;
	P.SpinePitch[2] = -2.f;
	P.SpinePitch[3] = -3.f;
	P.NeckPitch = -32.f;
	P.HeadPitch = -42.f;
	P.HeadYaw = 28.f;
	P.HeadRoll = 18.f;
	P.EarBack = 12.f;
	P.EarOut = 10.f;
	P.Lid = 1.f;
	for (int32 l = 0; l < 4; ++l)
	{
		const FVector H = Legs[l].Home;
		if (Legs[l].bFront)
		{
			P.Ball[l] = FVector(H.X - 3.8f, H.Y * 0.85f, 1.f) * M;
			P.Meta[l] = -85.f;
			P.Curl[l] = 10.f;
			P.Scap[l] = -6.f;
		}
		else
		{
			P.Ball[l] = FVector(H.X + 8.7f, H.Y * 1.45f, 1.1f) * M;
			P.Meta[l] = 88.f;
		}
	}
	static const float TP[8] = { -30.f, -46.f, -26.f, -8.f, -3.f, -2.f, -1.f, 0.f };
	static const float TY[8] = { 14.f, 30.f, 55.f, 80.f, 100.f, 120.f, 136.f, 146.f };
	// Even dozing, the tip of the tail twitches now and then.
	for (int32 i = 0; i < 8; ++i)
	{
		P.TailPitch[i] = TP[i] + FMath::Abs(TailFlick.Value) * 0.15f * Sat((i - 5) / 2.f);
		P.TailYaw[i] = TY[i] + 0.35f * TailFlick.Value * Sat((i - 4) / 3.f);
	}
	// Resting on the real ground under the cat (and drawn back with the body if its nose was against something).
	P.PelvisOffset.Z += GroundRig;
	for (int32 l = 0; l < 4; ++l)
	{
		P.Ball[l].Z += GroundRig;
		P.Ball[l].X += ClearX;
	}
}

void UShadeCatRig::UpdateIdleLife(float Dt)
{
	// Tail-tip flicks: a sprung kick that overshoots once and settles (the tail's own springs soften it further).
	FlickTimer -= Dt;
	if (FlickTimer <= 0.f)
	{
		FlickTimer = FMath::FRandRange(1.6f, 4.8f);
		TailFlick.Velocity += (FMath::RandBool() ? 1.f : -1.f) * FMath::FRandRange(180.f, 280.f);
	}
	TailFlick.Update(0.f, 60.f, 7.f, Dt);
	// Glances: somewhere else every few seconds (now and then straight ahead again), sometimes with a tilt of the head.
	GlanceTimer -= Dt;
	if (GlanceTimer <= 0.f)
	{
		GlanceTimer = FMath::FRandRange(1.4f, 4.2f);
		const bool bAhead = FMath::FRand() < 0.3f;
		GlanceYaw = bAhead ? 0.f : FMath::FRandRange(-38.f, 38.f);
		GlancePitch = bAhead ? 0.f : FMath::FRandRange(-9.f, 9.f);
		GlanceRoll = FMath::FRand() < 0.25f ? FMath::FRandRange(-9.f, 9.f) : 0.f;
	}
	GlanceYawS.Update(GlanceYaw, 55.f, 13.f, Dt);
	GlancePitchS.Update(GlancePitch, 55.f, 13.f, Dt);
	GlanceRollS.Update(GlanceRoll, 40.f, 11.f, Dt);
	// Sitting, a forepaw is shifted every so often.
	ShuffleT += Dt;
	ShuffleTimer -= Dt;
	if (ShuffleTimer <= 0.f)
	{
		ShuffleTimer = FMath::FRandRange(5.f, 11.f);
		if (TransTo == FamSit && TransS >= 1.f && ActW < 0.01f && bGrounded)
		{
			ShuffleT = 0.f;
			ShufflePaw = FMath::RandRange(0, 1);
		}
	}
}

// =====================================================================================================================
// Pose transitions
// =====================================================================================================================

void UShadeCatRig::FamilyParams(int32 Fam, const FCatPoseParams& Loco, FCatPoseParams& Dst) const
{
	switch (Fam)
	{
	case FamSit: ComputeSit(Dst); break;
	case FamSleep: ComputeSleep(Dst); break;
	case FamSnap: Dst = Snap; break;
	default: Dst = Loco; break;
	}
}

void UShadeCatRig::BlendShaped(const FCatPoseParams& A, const FCatPoseParams& B, FCatPoseParams& O) const
{
	const float M = MeshScale;
	const float S = TransS;
	auto Win = [](float X, float Lo, float Hi) { return ShadeEase::Smooth((X - Lo) / (Hi - Lo)); };
	// Weight toward B of each part of the body, and how high a paw that travels is lifted on the way.
	float WBody = ShadeEase::Smooth(S), WHead = WBody, WTail = WBody, WScap = WBody;
	float WPaw[4] = { WBody, WBody, WBody, WBody };
	float Arc[4] = { 0.f, 0.f, 0.f, 0.f };
	bool bLoaf = false;
	float LoafPaw[2] = { 0.f, 0.f };
	float LoafUp = 0.f;
	const int32 F = TransFrom, T = TransTo;
	if ((F == FamSleep && T == FamSit) || (F == FamSit && T == FamSleep))
	{
		// Up out of a loaf: the head comes up; the chest lifts a little off the folded forelegs; the forepaws come out
		// from under it one at a time and lie flat ahead (a sphinx); then the chest pushes up over them on straightening
		// forelegs; the hind paws shuffle into the sit and the tail wraps round last. Lying down is the same, backwards.
		const float U = T == FamSit ? S : 1.f - S;
		auto ToB = [T](float W) { return T == FamSit ? W : 1.f - W; };
		LoafUp = Win(U, 0.48f, 0.95f);
		LoafPaw[0] = Win(U, 0.1f, 0.36f);
		LoafPaw[1] = Win(U, 0.28f, 0.54f);
		WBody = WScap = ToB(0.15f * Win(U, 0.f, 0.25f) + 0.85f * LoafUp);
		WHead = ToB(Win(U, 0.f, 0.35f));
		WTail = ToB(Win(U, 0.45f, 1.f));
		WPaw[0] = ToB(LoafPaw[0]);
		WPaw[1] = ToB(LoafPaw[1]);
		WPaw[2] = ToB(Win(U, 0.55f, 0.92f));
		WPaw[3] = ToB(Win(U, 0.6f, 0.95f));
		Arc[0] = Arc[1] = 1.6f * M;
		Arc[2] = Arc[3] = 1.f * M;
		bLoaf = true;
	}
	else if (F == FamLoco && T == FamSit)
	{
		// Sitting down: the rump goes down first and the hind paws tuck forward under it, then the forepaws settle.
		WBody = WScap = Win(S, 0.f, 0.8f);
		WPaw[2] = Win(S, 0.05f, 0.6f);
		WPaw[3] = Win(S, 0.12f, 0.65f);
		WPaw[0] = Win(S, 0.45f, 0.82f);
		WPaw[1] = Win(S, 0.5f, 0.87f);
		WHead = ShadeEase::Smooth(S);
		WTail = Win(S, 0.35f, 1.f);
		Arc[0] = Arc[1] = 1.4f * M;
		Arc[2] = Arc[3] = 1.2f * M;
	}
	else if (F == FamSit && T == FamLoco)
	{
		// Getting up: tail and rump come up first and the chest levels; the paws stay planted (the gait steps them).
		WBody = WScap = Win(S, 0.f, 0.85f);
		WPaw[2] = WPaw[3] = Win(S, 0.f, 0.85f);
		WPaw[0] = WPaw[1] = Win(S, 0.1f, 0.9f);
		WHead = Win(S, 0.f, 0.7f);
		WTail = Win(S, 0.f, 0.6f);
	}

	O = B;
	O.PelvisOffset = FMath::Lerp(A.PelvisOffset, B.PelvisOffset, WBody);
	O.BodyPitch = FMath::Lerp(A.BodyPitch, B.BodyPitch, WBody);
	O.BodyRoll = FMath::Lerp(A.BodyRoll, B.BodyRoll, WBody);
	for (int32 i = 0; i < 4; ++i)
	{
		O.SpineYaw[i] = FMath::Lerp(A.SpineYaw[i], B.SpineYaw[i], WBody);
		O.SpinePitch[i] = FMath::Lerp(A.SpinePitch[i], B.SpinePitch[i], WBody);
	}
	O.NeckPitch = FMath::Lerp(A.NeckPitch, B.NeckPitch, WHead);
	O.HeadPitch = FMath::Lerp(A.HeadPitch, B.HeadPitch, WHead);
	O.HeadYaw = FMath::Lerp(A.HeadYaw, B.HeadYaw, WHead);
	O.HeadRoll = FMath::Lerp(A.HeadRoll, B.HeadRoll, WHead);
	O.EarBack = FMath::Lerp(A.EarBack, B.EarBack, WHead);
	O.EarOut = FMath::Lerp(A.EarOut, B.EarOut, WHead);
	O.Lid = FMath::Lerp(A.Lid, B.Lid, WHead);
	for (int32 l = 0; l < 4; ++l)
	{
		const float W = WPaw[l];
		O.Ball[l] = FMath::Lerp(A.Ball[l], B.Ball[l], W);
		O.Ball[l].Z += FMath::Min(Arc[l], 0.35f * FVector::Dist(A.Ball[l], B.Ball[l])) * FMath::Sin(PI * W);
		O.Meta[l] = FMath::Lerp(A.Meta[l], B.Meta[l], W);
		O.Curl[l] = FMath::Lerp(A.Curl[l], B.Curl[l], W);
	}
	if (bLoaf)
	{
		// Each forepaw: folded back under the chest -> flat on the ground ahead of it as it comes out -> upright under the
		// shoulder as the chest rises over it.
		const FCatPoseParams& PSleep = T == FamSit ? A : B;
		const FCatPoseParams& PSit = T == FamSit ? B : A;
		for (int32 l = 0; l < 2; ++l)
		{
			O.Meta[l] = FMath::Lerp(FMath::Lerp(PSleep.Meta[l], 75.f, LoafPaw[l]), PSit.Meta[l], LoafUp);
		}
	}
	O.Scap[0] = FMath::Lerp(A.Scap[0], B.Scap[0], WScap);
	O.Scap[1] = FMath::Lerp(A.Scap[1], B.Scap[1], WScap);
	for (int32 i = 0; i < 8; ++i)
	{
		O.TailPitch[i] = FMath::Lerp(A.TailPitch[i], B.TailPitch[i], WTail);
		O.TailYaw[i] = FMath::Lerp(A.TailYaw[i], B.TailYaw[i], WTail);
	}
}

// =====================================================================================================================
// Interaction layer: paws on a cart, a cord in both paws, a swat at a pot
// =====================================================================================================================

float UShadeCatRig::GetPawReach() const
{
	using namespace CatBone;
	// Shoulder ahead of the centre plus a bit over half the arm: the paw lands well inside its reach, never locked out.
	const float Arm = BoneLen[UArmL] + BoneLen[FArmL] + BoneLen[HandL];
	return (RestPos[UArmL].X + 0.55f * Arm) * MeshScale;
}

float UShadeCatRig::RearForHeight(float Height) const
{
	using namespace CatBone;
	const float M = MeshScale;
	const float Arm = (BoneLen[UArmL] + BoneLen[FArmL] + BoneLen[HandL]) * M;
	const float Shoulder = RestPos[UArmL].Z * M;
	// Standing, the paws comfortably reach a little above the shoulder; rearing lifts the shoulder by up to most of the
	// pelvis-to-shoulder length.
	const float Lift = FMath::Max((RestPos[UArmL] - RestPos[Pelvis]).Size() * M * 0.75f, 1.f);
	return Sat((Height - (Shoulder + 0.15f * Arm)) / Lift);
}

FVector UShadeCatRig::GripOffset(float MetaDeg, float CurlDeg) const
{
	// Same conventions as the leg solve: MDir = wrist -> ball, TDir = ball -> toe tips.
	const float A = FMath::DegreesToRadians(MetaDeg);
	const float Cu = FMath::DegreesToRadians(CurlDeg);
	const FVector Back(-FMath::Sin(A), 0.f, FMath::Cos(A));     // ball -> wrist
	const FVector Toes(FMath::Cos(Cu), 0.f, -FMath::Sin(Cu));   // ball -> toe tips
	const FVector Bis = Back + Toes;
	if (Bis.SizeSquared() < 0.04f)
	{
		return FVector::ZeroVector;  // an open, straight paw: nothing to close round
	}
	// A bar resting in the crook touches the toe pads and the back of the paw: out along the bisector by (bar radius +
	// pad thickness) / sin(half the crook angle).
	const float SinHalf = FMath::Sqrt(FMath::Max(0.5f * (1.f - FVector::DotProduct(Back, Toes)), 0.05f));
	const float R = GripBarRadius + 0.8f * MeshScale;
	return Bis.GetSafeNormal() * FMath::Min(R / SinHalf, 6.f * MeshScale);
}

FVector UShadeCatRig::BallForGrip(const FVector& GripWorld) const
{
	return GripWorld - RigToWorld.TransformVectorNoScale(GripOffset(ActMeta, ActCurl));
}

void UShadeCatRig::ApplyAction(FCatPoseParams& P)
{
	using namespace CatBone;
	const float W = Sat(ActW);
	bActFront[0] = bActFront[1] = false;
	if (W <= 0.001f)
	{
		return;
	}
	const float M = MeshScale;
	const float R = Sat(ActRear) * W;
	const float Lean = FMath::Clamp(ActLean, -1.f, 1.f) * W;
	// Up on the hind legs: rump lower and back, chest raised, neck bent forward so the head stays near level; the hocks
	// drop toward the ground as the weight goes onto them.
	P.PelvisOffset += FVector(-2.5f * R + 1.6f * Lean, 0.f, -4.f * R - 0.6f * FMath::Abs(Lean)) * M;
	P.BodyPitch += 50.f * R - 5.f * Lean;
	P.SpinePitch[1] += 3.f * R;
	P.SpinePitch[2] += 4.f * R - 2.f * Lean;
	P.SpinePitch[3] += 2.f * R - 2.f * Lean;
	P.NeckPitch -= 26.f * R + 6.f * Lean;
	P.HeadPitch -= 6.f * FMath::Max(Lean, 0.f);
	P.EarBack += 10.f * FMath::Abs(Lean) * W;
	for (int32 l = 2; l < 4; ++l)
	{
		P.Meta[l] += 34.f * R;
	}
	for (int32 l = 0; l < 4; ++l)
	{
		const float A = Sat(ActPawW[l]) * W;
		if (A <= 0.001f)
		{
			continue;
		}
		P.Ball[l] = FMath::Lerp(P.Ball[l], ToRig(ActPawWorld[l]), A);
		if (l >= 2)
		{
			// Hind paws braced against a wall: toes pressed flat to it.
			P.Meta[l] = FMath::Lerp(P.Meta[l], -60.f, A);
			P.Curl[l] = FMath::Lerp(P.Curl[l], -30.f, A);
			continue;
		}
		bActFront[l] = A > 0.5f;
		P.Meta[l] = FMath::Lerp(P.Meta[l], ActMeta, A);
		P.Curl[l] = FMath::Lerp(P.Curl[l], ActCurl, A);
		P.Scap[l] = FMath::Lerp(P.Scap[l], 6.f, A);
	}
	// Tail low and still with effort.
	for (int32 i = 0; i < 8; ++i)
	{
		P.TailPitch[i] = FMath::Lerp(P.TailPitch[i], -10.f + 2.f * i, 0.5f * W);
	}
}

// =====================================================================================================================
// Solve: params -> bone transforms (rig space)
// =====================================================================================================================

void UShadeCatRig::Solve(const FCatPoseParams& P)
{
	using namespace CatBone;
	const float M = MeshScale;
	for (FBoneOut& O : Out) { O.bSet = false; }
	auto Set = [this](int32 B, const FVector& Pos, const FQuat& Rot) { Out[B].Pos = Pos; Out[B].Rot = Rot; Out[B].bSet = true; };

	const FQuat BodyQ = FRotator(P.BodyPitch, 0.f, P.BodyRoll).Quaternion();
	const FVector Right = BodyQ.RotateVector(FVector::RightVector);

	// Spine chain from the rump forward (Bow: extra pitch at the mid-back, see below).
	const FVector PPel = RP(Pelvis) + P.PelvisOffset;
	const FQuat QPel = BodyQ * PY(P.SpinePitch[0], P.SpineYaw[0]);
	const FVector PS1 = PPel + QPel.RotateVector(RP(Spine1) - RP(Pelvis));
	FQuat Q1, Q2, Q3;
	FVector PS2, PS3;
	auto BuildSpine = [&](float Bow)
	{
		Q1 = QPel * PY(P.SpinePitch[1] + Bow, P.SpineYaw[1]);
		PS2 = PS1 + Q1.RotateVector(RP(Spine2) - RP(Spine1));
		Q2 = Q1 * PY(P.SpinePitch[2], P.SpineYaw[2]);
		PS3 = PS2 + Q2.RotateVector(RP(Spine3) - RP(Spine2));
		Q3 = Q2 * PY(P.SpinePitch[3], P.SpineYaw[3]);
	};
	BuildSpine(0.f);
	if (SeatedW > 0.001f && bGrounded)
	{
		// Sitting, lying or getting up, the forepaws stay on the ground: if the chest is carried higher than the forelegs
		// reach, it bows forward until they do (with a little bend left in the elbows) - a paw never hangs in the air.
		auto FrontOver = [&]()
		{
			float Ov = -1e6f;
			for (int32 l = 0; l < 2; ++l)
			{
				if (bActFront[l])
				{
					continue;
				}
				const int32* C = GLegBones[l];
				const FVector ScapTop = PS3 + Q3.RotateVector(RP(C[0]) - RP(Spine3));
				const FVector Root = ScapTop + (Q3 * PY(P.Scap[l], 0.f)).RotateVector(RP(C[1]) - RP(C[0]));
				const float A = FMath::DegreesToRadians(P.Meta[l]);
				const FVector WristT = P.Ball[l] - FVector(FMath::Sin(A), 0.f, -FMath::Cos(A)) * (BoneLen[C[3]] * M);
				Ov = FMath::Max(Ov, FVector::Dist(Root, WristT) - 0.97f * (BoneLen[C[1]] + BoneLen[C[2]]) * M);
			}
			return Ov;
		};
		float Bow = 0.f;
		for (int32 It = 0; It < 3; ++It)
		{
			const float Ov = FrontOver();
			if (Ov <= 0.f)
			{
				break;
			}
			BuildSpine(Bow - 2.f);
			const float PerDeg = (Ov - FrontOver()) / 2.f;
			if (PerDeg < 0.02f)
			{
				break;
			}
			Bow -= FMath::Min(Ov / PerDeg, 25.f);
			BuildSpine(Bow);
		}
		BuildSpine(FMath::Max(Bow, -30.f) * Sat(SeatedW * 4.f));
	}
	Set(Pelvis, PPel, QPel * RestRot[Pelvis]);
	TailRootRig = PPel + QPel.RotateVector(RP(Tail1) - RP(Pelvis));
	TailPelvisDelta = QPel;
	Set(Spine1, PS1, Q1 * RestRot[Spine1]);
	Set(Spine2, PS2, Q2 * RestRot[Spine2]);
	Set(Spine3, PS3, Q3 * RestRot[Spine3]);

	// Neck halfway between chest and gaze; head holds the gaze (stabilised against the body bob).
	const FQuat QLook = FRotator(P.HeadPitch, P.HeadYaw, P.HeadRoll).Quaternion();
	const FVector PNeck = PS3 + Q3.RotateVector(RP(Neck) - RP(Spine3));
	const FQuat QNeck = FQuat::Slerp(Q3, QLook, 0.45f) * PY(P.NeckPitch, 0.f);
	const FVector PHead = PNeck + QNeck.RotateVector(RP(Head) - RP(Neck));
	const FQuat QHead = FQuat::Slerp(QNeck, QLook, 0.8f);
	Set(Neck, PNeck, QNeck * RestRot[Neck]);
	Set(Head, PHead, QHead * RestRot[Head]);
	for (int32 E : { (int32)EarL, (int32)EarR })
	{
		const float Side = FMath::Sign(RestPos[E].Y);
		const FShadeSpring& Tw = EarTwitch[E == EarL ? 0 : 1];
		const FQuat Local = FRotator(P.EarBack + Tw.Value * 0.035f, Side * Tw.Value * 0.01f, Side * (P.EarOut + Tw.Value * 0.02f)).Quaternion();
		Set(E, PHead + QHead.RotateVector(RP(E) - RP(Head)), QHead * Local * RestRot[E]);
	}

	// Legs.
	float Over = 0.f;
	for (int32 l = 0; l < 4; ++l)
	{
		const int32* C = GLegBones[l];
		const FCatLeg& L = Legs[l];
		const float A = FMath::DegreesToRadians(P.Meta[l]);
		const FVector MDir(FMath::Sin(A), 0.f, -FMath::Cos(A));
		const float Cu = FMath::DegreesToRadians(P.Curl[l]);
		const FVector TDir(FMath::Cos(Cu), 0.f, -FMath::Sin(Cu));
		const float Lmeta = BoneLen[C[3]] * M;
		const float L1 = BoneLen[C[1]] * M;
		const float L2 = BoneLen[C[2]] * M;
		const FVector Ball = P.Ball[l];
		FVector Root, Mid, End;
		if (L.bFront)
		{
			const FVector ScapTop = PS3 + Q3.RotateVector(RP(C[0]) - RP(Spine3));
			const FQuat QScap = Q3 * PY(P.Scap[l], 0.f);
			Root = ScapTop + QScap.RotateVector(RP(C[1]) - RP(C[0]));
			Set(C[0], ScapTop, Aim(C[0], Root - ScapTop, Right));
			const FVector WristT = Ball - MDir * Lmeta;
			if (!bActFront[l])  // a paw held on a target out of reach must not drag the body down
			{
				Over = FMath::Max(Over, FVector::Dist(Root, WristT) - 0.985f * (L1 + L2));
			}
			SolveTwoBone(Root, WristT, L1, L2, FVector(-1.f, 0.12f * L.Side, 0.f), Mid, End);
		}
		else
		{
			Root = PPel + QPel.RotateVector(RP(C[1]) - RP(Pelvis));
			const FVector HockT = Ball - MDir * Lmeta;
			Over = FMath::Max(Over, FVector::Dist(Root, HockT) - 0.985f * (L1 + L2));
			SolveTwoBone(Root, HockT, L1, L2, FVector(1.f, 0.1f * L.Side, 0.f), Mid, End);
		}
		const FVector BallOut = End + MDir * Lmeta;
		if (l < 2)
		{
			PawOut[l] = BallOut;
			GripOut[l] = BallOut + GripOffset(P.Meta[l], P.Curl[l]);
		}
		Set(C[1], Root, Aim(C[1], Mid - Root, Right));
		Set(C[2], Mid, Aim(C[2], End - Mid, Right));
		Set(C[3], End, Aim(C[3], MDir, FVector::RightVector));
		Set(C[4], BallOut, Aim(C[4], TDir, FVector::RightVector));
	}
	// Legs that cannot reach lower the body a touch (and it rises back when they can).
	const float Want = (Over > 0.f && bGrounded) ? LowerFix.Value - Over : FMath::Min(0.f, LowerFix.Value + 0.5f * M);  // (in the air the legs stretch freely)
	LowerFix.Value = FMath::Lerp(LowerFix.Value, FMath::Clamp(Want, -2.5f * M, 0.f), 0.12f);
}

void UShadeCatRig::UpdateTail(float Dt, const FCatPoseParams& P, const FVector& RootRig, const FQuat& PelvisDelta)
{
	using namespace CatBone;
	const float M = MeshScale;
	const FVector RootW = ToWorld(RootRig);
	float Len[8];
	for (int32 i = 0; i < 8; ++i) { Len[i] = BoneLen[Tail1 + i] * M; }
	auto TargetDirW = [&](int32 i)
	{
		return RigToWorld.TransformVectorNoScale(TailFrame.RotateVector(TailDir(P.TailPitch[i], P.TailYaw[i]))).GetSafeNormal();
	};
	if (!bTailInit || FVector::Dist(RootW, TailX[0]) > 60.f * M)
	{
		TailFrame = PelvisDelta;
		for (int32 i = 0; i < 8; ++i)
		{
			TailDirW[i] = TargetDirW(i);
			TailOmega[i] = FVector::ZeroVector;
		}
		TailRootVel = TailRootAcc = FVector::ZeroVector;
		TailX[0] = RootW;
		bTailInit = true;
	}
	// Root motion -> inertia. Low-passed so the step bob only nudges the tail instead of shaking it.
	const FVector RootVel = (RootW - TailX[0]) / Dt;
	const FVector RawAcc = ((RootVel - TailRootVel) / Dt).GetClampedToMaxSize(1400.f * M);
	TailRootVel = RootVel;
	TailRootAcc = FMath::Lerp(TailRootAcc, RawAcc, 1.f - FMath::Exp(-9.f * Dt));
	TailX[0] = RootW;
	TailFrame = FQuat::Slerp(TailFrame, PelvisDelta, 1.f - FMath::Exp(-10.f * Dt));

	// Every segment's world direction is its own damped angular spring toward the pose direction. Segments do not
	// chase each other (a chain of followers resonates and whips), so the base stays steady and the tip simply lags
	// further behind turns and starts / stops - follow-through without any instability.
	const int32 Sub = 3;
	const float H = Dt / Sub;
	for (int32 s = 0; s < Sub; ++s)
	{
		for (int32 i = 0; i < 8; ++i)
		{
			const float A = i / 7.f;
			const float W0 = FMath::Lerp(17.f, 7.5f, A);
			const float Zeta = FMath::Lerp(1.f, 0.72f, A);
			const FVector T = TargetDirW(i);
			FVector& D = TailDirW[i];
			FVector& Om = TailOmega[i];
			const FVector Axis = FVector::CrossProduct(D, T);
			const float SinA = Axis.Size();
			const float Ang = FMath::Atan2(SinA, FVector::DotProduct(D, T));
			const FVector Err = SinA > 1e-6f ? Axis * (Ang / SinA) : FVector::ZeroVector;
			// Pseudo-force of the accelerating root on the segment's mass, as a pendulum of the segment's distance.
			const float Arm = (i + 0.5f) * Len[0];
			const FVector Inertia = FVector::CrossProduct(D, -TailRootAcc) * (0.32f / FMath::Max(Arm, 1.f));
			Om += (Err * (W0 * W0) - Om * (2.f * Zeta * W0) + Inertia) * H;
			Om -= D * FVector::DotProduct(Om, D);
			Om = Om.GetClampedToMaxSize(22.f);
			const float Turn = Om.Size() * H;
			if (Turn > 1e-6f)
			{
				D = FQuat(Om / Om.Size(), Turn).RotateVector(D).GetSafeNormal();
			}
		}
	}
	// Positions from the directions; keep the tail above the ground plane under the cat (output only).
	for (int32 i = 1; i < 9; ++i)
	{
		TailX[i] = TailX[i - 1] + TailDirW[i - 1] * Len[i - 1];
		FVector R = ToRig(TailX[i]);
		if (R.Z < GroundRig + 1.1f * M)
		{
			R.Z = GroundRig + 1.1f * M;
			TailX[i] = TailX[i - 1] + (ToWorld(R) - TailX[i - 1]).GetSafeNormal() * Len[i - 1];
		}
	}
	// Twist is carried down the chain (parallel transport) so a tail curling sideways never flips its roll.
	FVector Side = PelvisDelta.RotateVector(FVector::RightVector);
	for (int32 i = 0; i < 8; ++i)
	{
		const FVector A = ToRig(TailX[i]);
		const FVector B = ToRig(TailX[i + 1]);
		const FVector D = (B - A).GetSafeNormal(UE_SMALL_NUMBER, FVector::BackwardVector);
		FVector S2 = Side - D * FVector::DotProduct(Side, D);
		if (!S2.Normalize())
		{
			S2 = FVector::CrossProduct(FVector::UpVector, D).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
		}
		Side = S2;
		Out[Tail1 + i].Pos = A;
		Out[Tail1 + i].Rot = Aim(Tail1 + i, D, Side);
		Out[Tail1 + i].bSet = true;
	}
}

void UShadeCatRig::Apply()
{
	UPoseableMeshComponent* Mc = Mesh.Get();
	const int32 N = RefLocal.Num();
	if (!Mc || Mc->BoneSpaceTransforms.Num() != N)
	{
		if (Mc)
		{
			Mc->BoneSpaceTransforms = RefLocal;
		}
		return;
	}
	TArray<int32, TInlineAllocator<64>> BoneOf;
	BoneOf.Init(INDEX_NONE, N);
	for (int32 b = 0; b < CatBone::Count; ++b)
	{
		if (BoneIndex[b] != INDEX_NONE) { BoneOf[BoneIndex[b]] = b; }
	}
	const float InvS = 1.f / FMath::Max(UnitScale * MeshScale, 1e-6f);
	for (int32 i = 0; i < N; ++i)
	{
		const int32 b = BoneOf[i];
		if (b != INDEX_NONE && Out[b].bSet && !Out[b].Rot.ContainsNaN() && !Out[b].Pos.ContainsNaN())
		{
			CompPose[i] = FTransform(RigToComp * Out[b].Rot, RigToComp.RotateVector(Out[b].Pos) * InvS, RefComp[i].GetScale3D());
		}
		else
		{
			CompPose[i] = Parents[i] >= 0 ? RefLocal[i] * CompPose[Parents[i]] : RefLocal[i];
		}
	}
	for (int32 i = 0; i < N; ++i)
	{
		Mc->BoneSpaceTransforms[i] = Parents[i] >= 0 ? CompPose[i].GetRelativeTransform(CompPose[Parents[i]]) : CompPose[i];
	}
	Mc->MarkRefreshTransformDirty();
	Mc->RefreshBoneTransforms();
}
