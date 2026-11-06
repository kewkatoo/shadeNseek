#include "ShadeHUD.h"
#include "shadeNseek.h"
#include "ShadeTypes.h"
#include "ShadeAssets.h"
#include "ShadeGameMode.h"
#include "ShadePlayerController.h"
#include "ShadeCharacter.h"
#include "ShadeInteractables.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "ShadeHUD"

// =====================================================================================================================
// Design tokens: sun-bleached Mediterranean - cream plaster, terracotta, lemon, sea blue, shadow violet.
// =====================================================================================================================
namespace UI
{
	const FLinearColor Cream(0.97f, 0.92f, 0.82f, 1.f);
	const FLinearColor Paper(0.95f, 0.89f, 0.76f, 1.f);
	const FLinearColor Ink(0.07f, 0.11f, 0.19f, 1.f);
	const FLinearColor InkSoft(0.22f, 0.25f, 0.32f, 1.f);
	const FLinearColor Terracotta(0.78f, 0.33f, 0.17f, 1.f);
	const FLinearColor Lemon(1.f, 0.80f, 0.22f, 1.f);
	const FLinearColor Sea(0.10f, 0.40f, 0.62f, 1.f);
	const FLinearColor Olive(0.42f, 0.48f, 0.22f, 1.f);
	const FLinearColor Violet(0.17f, 0.12f, 0.28f, 1.f);
	const FLinearColor Sun(1.f, 0.62f, 0.2f, 1.f);
	const FLinearColor Black(0.f, 0.f, 0.f, 1.f);
	constexpr float Margin = 56.f;
}

namespace
{
	const FSlateColorBrush GSolid(FLinearColor::White);
	const FSlateRoundedBoxBrush GRound6(FLinearColor::White, 6.f);
	const FSlateRoundedBoxBrush GRound14(FLinearColor::White, 14.f);
	const FSlateRoundedBoxBrush GRound30(FLinearColor::White, 30.f);
	const FSlateRoundedBoxBrush GOutline6(FLinearColor::Transparent, 6.f, FLinearColor::White, 1.5f);
	const FSlateRoundedBoxBrush GOutline14(FLinearColor::Transparent, 14.f, FLinearColor::White, 1.5f);

	FLinearColor A(FLinearColor C, float Alpha)
	{
		C.A *= FMath::Clamp(Alpha, 0.f, 1.f);
		return C;
	}

	struct FPainter
	{
		const FGeometry& Geo;
		FSlateWindowElementList& Out;
		int32 Layer;
		float S;
		FVector2D Size;
		FSlateResourceHandle Solid;

		FPainter(const FGeometry& InGeo, FSlateWindowElementList& InOut, int32 InLayer)
			: Geo(InGeo), Out(InOut), Layer(InLayer)
		{
			const FVector2D Local = InGeo.GetLocalSize();
			S = FMath::Max(Local.Y / 1080.f, 0.1f);
			Size = Local / S;
			Solid = FSlateApplication::Get().GetRenderer()->GetResourceHandle(GSolid);
		}

		FPaintGeometry PG(const FVector2D& Pos, const FVector2D& Sz) const
		{
			return Geo.ToPaintGeometry(FVector2f(Sz * S), FSlateLayoutTransform(FVector2f(Pos * S)));
		}

		void Box(const FVector2D& Pos, const FVector2D& Sz, const FLinearColor& C, const FSlateBrush* Brush = &GSolid, int32 L = 0) const
		{
			if (C.A <= 0.002f || Sz.X <= 0.f || Sz.Y <= 0.f) { return; }
			FSlateDrawElement::MakeBox(Out, Layer + L, PG(Pos, Sz), Brush, ESlateDrawEffect::None, C);
		}

		void Gradient(const FVector2D& Pos, const FVector2D& Sz, const FLinearColor& From, const FLinearColor& To, EOrientation Orient, int32 L = 0) const
		{
			if (From.A <= 0.002f && To.A <= 0.002f) { return; }
			TArray<FSlateGradientStop> Stops;
			Stops.Add(FSlateGradientStop(FVector2D::ZeroVector, From));
			Stops.Add(FSlateGradientStop(Sz * S, To));
			FSlateDrawElement::MakeGradient(Out, Layer + L, PG(Pos, Sz), Stops, Orient);
		}

		void Poly(const TArray<FVector2D>& Pts, const FLinearColor& C, int32 L = 0) const
		{
			if (C.A <= 0.002f || Pts.Num() < 3) { return; }
			const FSlateRenderTransform& RT = Geo.GetAccumulatedRenderTransform();
			const FColor Col = C.ToFColor(true);
			TArray<FSlateVertex> Verts;
			TArray<SlateIndex> Idx;
			for (const FVector2D& P : Pts)
			{
				Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RT, FVector2f(P * S), FVector2f(0.5f, 0.5f), Col));
			}
			for (int32 i = 1; i + 1 < Pts.Num(); ++i)
			{
				Idx.Add(0);
				Idx.Add(i);
				Idx.Add(i + 1);
			}
			FSlateDrawElement::MakeCustomVerts(Out, Layer + L, Solid, Verts, Idx, nullptr, 0, 0);
		}

		void Circle(const FVector2D& C, float R, const FLinearColor& Col, int32 L = 0, int32 Seg = 28) const
		{
			TArray<FVector2D> Pts;
			for (int32 i = 0; i < Seg; ++i)
			{
				const float Ang = 2.f * PI * i / Seg;
				Pts.Add(C + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * R);
			}
			Poly(Pts, Col, L);
		}

		void Diamond(const FVector2D& C, float R, const FLinearColor& Col, int32 L = 0) const
		{
			Poly({ C + FVector2D(0, -R), C + FVector2D(R, 0), C + FVector2D(0, R), C + FVector2D(-R, 0) }, Col, L);
		}

		void Lines(const TArray<FVector2D>& Pts, const FLinearColor& C, float T = 1.5f, int32 L = 1) const
		{
			if (C.A <= 0.002f || Pts.Num() < 2) { return; }
			TArray<FVector2f> Local;
			Local.Reserve(Pts.Num());
			for (const FVector2D& P : Pts) { Local.Add(FVector2f(P * S)); }
			FSlateDrawElement::MakeLines(Out, Layer + L, Geo.ToPaintGeometry(), Local, ESlateDrawEffect::None, C, true, T * S);
		}

		void Arc(const FVector2D& Center, float Radius, float From01, float To01, const FLinearColor& C, float T, int32 L = 2) const
		{
			TArray<FVector2D> Pts;
			const int32 Seg = FMath::Max(2, FMath::CeilToInt(FMath::Abs(To01 - From01) * 64.f));
			for (int32 i = 0; i <= Seg; ++i)
			{
				const float Ang = (-0.25f + FMath::Lerp(From01, To01, static_cast<float>(i) / Seg)) * 2.f * PI;
				Pts.Add(Center + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * Radius);
			}
			Lines(Pts, C, T, L);
		}

		/** Soft dark backing (stacked rounded boxes fake a blur falloff). */
		void Soft(const FVector2D& Pos, const FVector2D& Sz, float Alpha, int32 Steps = 14, const FLinearColor& Tint = FLinearColor(0.02f, 0.03f, 0.06f, 1.f)) const
		{
			const float Inset = 3.f;
			for (int32 i = 0; i < Steps; ++i)
			{
				const FVector2D O(Inset * i, Inset * i);
				if (Sz.X - O.X * 2.f <= 0.f || Sz.Y - O.Y * 2.f <= 0.f) { break; }
				Box(Pos + O, Sz - O * 2.f, FLinearColor(Tint.R, Tint.G, Tint.B, Alpha / Steps * 1.5f), &GRound30);
			}
		}

		FVector2D Measure(const FString& Str, const FSlateFontInfo& Font) const
		{
			const TSharedRef<FSlateFontMeasure> M = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			return M->Measure(Str, Font) / S;
		}

		/** Align 0 = left/top, 0.5 = centre, 1 = right/bottom. */
		FVector2D Text(const FString& Str, const FVector2D& Pos, const FSlateFontInfo& Font, const FLinearColor& C, float AX = 0.f, float AY = 0.f, float ShadowA = 0.45f, int32 L = 3) const
		{
			const FVector2D Sz = Measure(Str, Font);
			if (C.A > 0.002f && !Str.IsEmpty())
			{
				const FVector2D At(Pos.X - Sz.X * AX, Pos.Y - Sz.Y * AY);
				if (ShadowA > 0.f)
				{
					FSlateDrawElement::MakeText(Out, Layer + L - 1, PG(At + FVector2D(1.5f, 2.f), Sz), Str, Font, ESlateDrawEffect::None, FLinearColor(0.02f, 0.02f, 0.05f, ShadowA * C.A));
				}
				FSlateDrawElement::MakeText(Out, Layer + L, PG(At, Sz), Str, Font, ESlateDrawEffect::None, C);
			}
			return Sz;
		}

		/** Hairline rule that fades out at both ends. */
		void Rule(const FVector2D& Pos, float W, const FLinearColor& C, float T = 1.5f, int32 L = 4) const
		{
			if (W <= 1.f || C.A <= 0.002f) { return; }
			const float F = FMath::Min(W * 0.18f, 60.f);
			FLinearColor Z = C; Z.A = 0.f;
			Gradient(Pos, FVector2D(F, T), Z, C, Orient_Vertical, L);
			Box(Pos + FVector2D(F, 0.f), FVector2D(W - 2.f * F, T), C, &GSolid, L);
			Gradient(Pos + FVector2D(W - F, 0.f), FVector2D(F, T), C, Z, Orient_Vertical, L);
		}

		void Ring(const FVector2D& C, float R, const FLinearColor& Col, float T = 1.5f, int32 L = 4) const
		{
			Arc(C, R, 0.f, 1.f, Col, T, L);
		}

		/** Display type with a soft, low-sun shadow (several faint offset copies read as a blur). */
		FVector2D Display(const FString& Str, const FVector2D& Pos, const FSlateFontInfo& Font, const FLinearColor& C, float AX = 0.f, float AY = 0.f, float ShadowA = 0.5f, int32 L = 6) const
		{
			const FVector2D Sz = Measure(Str, Font);
			if (C.A > 0.002f && !Str.IsEmpty())
			{
				const FVector2D At(Pos.X - Sz.X * AX, Pos.Y - Sz.Y * AY);
				const FVector2D Offs[4] = { FVector2D(1.f, 3.f), FVector2D(3.f, 6.f), FVector2D(5.f, 10.f), FVector2D(8.f, 15.f) };
				for (int32 i = 0; i < 4; ++i)
				{
					FSlateDrawElement::MakeText(Out, Layer + L - 1, PG(At + Offs[i], Sz), Str, Font, ESlateDrawEffect::None, FLinearColor(0.02f, 0.015f, 0.04f, ShadowA * C.A * (0.32f - i * 0.06f)));
				}
				FSlateDrawElement::MakeText(Out, Layer + L, PG(At, Sz), Str, Font, ESlateDrawEffect::None, C);
			}
			return Sz;
		}

		/** A glazed-tile card: cream body, terracotta rim, painted corner diamonds. */
		void Tile(const FVector2D& Pos, const FVector2D& Sz, float Alpha, const FLinearColor& Rim = UI::Terracotta) const
		{
			// Solid layers (outline brushes tint their fill too): shadow, cream body, thin inset glaze line, cream centre.
			Box(Pos + FVector2D(3.f, 5.f), Sz, A(FLinearColor(0.f, 0.f, 0.02f, 1.f), 0.28f * Alpha), &GRound14, 0);
			Box(Pos, Sz, A(UI::Cream, 0.97f * Alpha), &GRound14, 1);
			Box(Pos + FVector2D(6.f, 6.f), Sz - FVector2D(12.f, 12.f), A(Rim, 0.9f * Alpha), &GRound14, 2);
			Box(Pos + FVector2D(8.f, 8.f), Sz - FVector2D(16.f, 16.f), A(UI::Cream, 0.98f * Alpha), &GRound14, 2);
			const float R = 5.f;
			for (const FVector2D& Corner : { Pos + FVector2D(14, 14), Pos + FVector2D(Sz.X - 14, 14), Pos + FVector2D(14, Sz.Y - 14), Pos + Sz - FVector2D(14, 14) })
			{
				Diamond(Corner, R, A(UI::Sea, 0.8f * Alpha), 3);
				Diamond(Corner, R * 0.45f, A(UI::Lemon, 0.9f * Alpha), 4);
			}
		}

		/** Keycap: cream hairline around dark glass, cream glyph. Returns width. */
		float Key(const FString& Label, const FVector2D& Pos, const FSlateFontInfo& Font, float Alpha, float Height = 30.f) const
		{
			const FVector2D T = Measure(Label, Font);
			const float W = FMath::Max(Height, static_cast<float>(T.X) + 18.f);
			Box(Pos, FVector2D(W, Height), A(UI::Cream, 0.8f * Alpha), &GRound6, 4);
			Box(Pos + FVector2D(1.3f, 1.3f), FVector2D(W - 2.6f, Height - 2.6f), A(FLinearColor(0.03f, 0.035f, 0.06f, 1.f), 0.72f * Alpha), &GRound6, 5);
			Text(Label, Pos + FVector2D(W * 0.5f, Height * 0.5f), Font, A(UI::Cream, Alpha), 0.5f, 0.5f, 0.f, 7);
			return W;
		}

		/** Tiny painted sun: disc + rays; Fill tints it from pale to blazing. */
		void SunGlyph(const FVector2D& C, float R, float Fill, float Alpha, float Spin) const
		{
			const FLinearColor Col = FMath::Lerp(UI::Lemon, UI::Sun, Fill);
			for (int32 i = 0; i < 10; ++i)
			{
				const float Ang = Spin + i * 2.f * PI / 10.f;
				const FVector2D D(FMath::Cos(Ang), FMath::Sin(Ang));
				const FVector2D N(-D.Y, D.X);
				const float L0 = R * 1.25f, L1 = R * (1.65f + 0.25f * Fill);
				Poly({ C + D * L0 + N * R * 0.16f, C + D * L1, C + D * L0 - N * R * 0.16f }, A(Col, Alpha), 3);
			}
			Circle(C, R, A(Col, Alpha), 3);
			Circle(C, R * 0.62f, A(FMath::Lerp(UI::Cream, UI::Lemon, Fill), Alpha), 4);
		}

		/** Little cat-head silhouette (for Shade / Biscotto icons). */
		void CatGlyph(const FVector2D& C, float R, const FLinearColor& Col, int32 L = 3) const
		{
			Circle(C, R, Col, L);
			Poly({ C + FVector2D(-R * 0.95f, -R * 0.2f), C + FVector2D(-R * 0.75f, -R * 1.35f), C + FVector2D(-R * 0.15f, -R * 0.8f) }, Col, L);
			Poly({ C + FVector2D(R * 0.95f, -R * 0.2f), C + FVector2D(R * 0.75f, -R * 1.35f), C + FVector2D(R * 0.15f, -R * 0.8f) }, Col, L);
		}
	};

	FString FormatTime(float Seconds)
	{
		const int32 T = FMath::Max(0, FMath::FloorToInt(Seconds));
		return FString::Printf(TEXT("%d:%02d"), T / 60, T % 60);
	}
}

// =====================================================================================================================
// Overlay widget
// =====================================================================================================================

class SShadeOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SShadeOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AShadeHUD>, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		Owner = Args._Owner;
		SetCanTick(true);
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.f, 1080.f); }
	virtual bool SupportsKeyboardFocus() const override { return true; }

	virtual void Tick(const FGeometry& Geo, const double CurrentTime, const float DeltaTime) override
	{
		SLeafWidget::Tick(Geo, CurrentTime, DeltaTime);
		Dt = FMath::Clamp(DeltaTime, 0.f, 0.1f);
		UpdateSmoothing();
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnKeyDown(const FGeometry& Geo, const FKeyEvent& Key) override;
	virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Mouse) override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Mouse) override;

private:
	AShadeGameMode* GM() const
	{
		AShadeHUD* H = Owner.Get();
		return H && H->GetWorld() ? H->GetWorld()->GetAuthGameMode<AShadeGameMode>() : nullptr;
	}
	AShadePlayerController* PC() const
	{
		AShadeHUD* H = Owner.Get();
		return H ? Cast<AShadePlayerController>(H->GetOwningPlayerController()) : nullptr;
	}
	AShadeCharacter* Shade() const
	{
		AShadePlayerController* P = PC();
		return P ? Cast<AShadeCharacter>(P->GetPawn()) : nullptr;
	}
	FSlateFontInfo F(FName Face, float Size, int32 Spacing, float S) const
	{
		AShadeHUD* H = Owner.Get();
		return H ? H->Font(Face, Size * S, Spacing) : FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), FMath::RoundToInt(Size * S));
	}
	FString KeyLabel(const TCHAR* Kbd, const TCHAR* Pad) const
	{
		const AShadePlayerController* P = PC();
		return (P && P->IsUsingGamepad()) ? FString(Pad) : FString(Kbd);
	}
	bool Project(const FPainter& P, const FVector& World, FVector2D& Out) const;
	void UpdateSmoothing();

	void PaintObjective(const FPainter& P, double Now) const;
	void PaintPrompt(const FPainter& P, double Now) const;
	void PaintMarker(const FPainter& P, double Now) const;
	void PaintExposure(const FPainter& P, double Now) const;
	void PaintHealth(const FPainter& P, double Now) const;
	void PaintLine(const FPainter& P, double Now) const;
	void PaintToasts(const FPainter& P, double Now) const;
	void PaintControls(const FPainter& P, float Alpha, const FVector2D& Origin) const;
	void PaintTitle(const FPainter& P, double Now) const;
	void PaintPause(const FPainter& P, double Now) const;
	void PaintEnd(const FPainter& P, double Now) const;
	void PaintMenuItems(const FPainter& P, double Now, const FVector2D& Origin, float Alpha, bool bCentered) const;

	TWeakObjectPtr<AShadeHUD> Owner;
	float Dt = 0.f;
	float HudAlpha = 0.f;
	float PromptAlpha = 0.f;
	float MarkerAlpha = 0.f;
	float ExposureShown = 0.f;
	float HealthShown = 1.f;
	float HealthGhost = 1.f;
	float FlashAlpha = 0.f;
	bool bWasEvap = false;
	TWeakObjectPtr<AShadeInteractable> PromptTarget;
	mutable TArray<FBox2D> ItemRects;
	mutable float LastS = 1.f;
};

void SShadeOverlay::UpdateSmoothing()
{
	AShadeGameMode* G = GM();
	AShadeCharacter* K = Shade();
	if (!G || !K)
	{
		return;
	}
	const bool bPlay = G->GetPhase() == EShadePhase::Playing || G->GetPhase() == EShadePhase::Finale;
	HudAlpha = ShadeEase::Approach(HudAlpha, bPlay ? 1.f : 0.f, 3.f, Dt);
	AShadeInteractable* I = bPlay ? K->GetFocused() : nullptr;
	if (I && !I->Verb.IsEmpty())
	{
		PromptTarget = I;
	}
	const bool bPrompt = I && !I->Verb.IsEmpty();
	PromptAlpha = ShadeEase::Approach(PromptAlpha, bPrompt ? 1.f : 0.f, bPrompt ? 10.f : 7.f, Dt);
	MarkerAlpha = ShadeEase::Approach(MarkerAlpha, (bPlay && !bPrompt) ? 1.f : 0.f, 4.f, Dt);
	ExposureShown = ShadeEase::Approach(ExposureShown, K->GetExposure(), 14.f, Dt);
	HealthShown = ShadeEase::Approach(HealthShown, K->GetHealth(), 14.f, Dt);
	// The lost chunk lingers in sun-gold for a moment, then drains away after it.
	HealthGhost = HealthGhost > HealthShown ? ShadeEase::Approach(HealthGhost, HealthShown, 1.6f, Dt) : HealthShown;
	if (K->IsEvaporating() && !bWasEvap)
	{
		FlashAlpha = 1.f;
	}
	bWasEvap = K->IsEvaporating();
	FlashAlpha = FMath::Max(0.f, FlashAlpha - Dt * 1.6f);
}

bool SShadeOverlay::Project(const FPainter& P, const FVector& World, FVector2D& Out) const
{
	AShadePlayerController* Ctrl = PC();
	if (!Ctrl || !Ctrl->PlayerCameraManager)
	{
		return false;
	}
	FVector2D Viewport(1920.f, 1080.f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Viewport);
	}
	const FVector Local = Ctrl->PlayerCameraManager->GetCameraRotation().UnrotateVector(World - Ctrl->PlayerCameraManager->GetCameraLocation());
	FVector2D Screen;
	if (Local.X < 10.f || !Ctrl->ProjectWorldLocationToScreen(World, Screen, true))
	{
		return false;
	}
	Out = Screen / Viewport * P.Size;
	return Out.X > 40.f && Out.Y > 40.f && Out.X < P.Size.X - 40.f && Out.Y < P.Size.Y - 40.f;
}

int32 SShadeOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
	FPainter P(Geo, Out, LayerId);
	LastS = P.S;
	const double Now = FPlatformTime::Seconds();
	ItemRects.Reset();
	AShadeGameMode* G = GM();
	AShadeHUD* H = Owner.Get();
	if (!G || !H)
	{
		return LayerId;
	}

	// Letterbox (cinematics).
	const float LB = ShadeEase::InOutCubic(G->GetLetterbox()) * 0.1f * P.Size.Y;
	if (LB > 0.5f)
	{
		P.Box(FVector2D(0, 0), FVector2D(P.Size.X, LB), FLinearColor(0.01f, 0.01f, 0.02f, 1.f), &GSolid, 20);
		P.Box(FVector2D(0, P.Size.Y - LB), FVector2D(P.Size.X, LB), FLinearColor(0.01f, 0.01f, 0.02f, 1.f), &GSolid, 20);
	}

	PaintExposure(P, Now);
	if (HudAlpha > 0.01f)
	{
		PaintObjective(P, Now);
		PaintHealth(P, Now);
		PaintMarker(P, Now);
		PaintPrompt(P, Now);
		PaintToasts(P, Now);
		// Controls appear once the opening line has been read, and fade after a while.
		const float Intro = ShadeEase::Window(G->GetPhaseTime(), 5.6f, 6.6f) * (1.f - ShadeEase::Window(G->GetRunTime(), 24.f, 26.f));
		if (G->GetPhase() == EShadePhase::Playing && Intro > 0.01f)
		{
			PaintControls(P, Intro * HudAlpha, FVector2D(UI::Margin, P.Size.Y - UI::Margin - 40.f));
		}
	}
	PaintLine(P, Now);

	switch (H->GetMenu())
	{
	case EShadeMenu::Title: PaintTitle(P, Now); break;
	case EShadeMenu::Pause: PaintPause(P, Now); break;
	case EShadeMenu::End: PaintEnd(P, Now); break;
	default: break;
	}

	// Evaporation flash + fade.
	if (FlashAlpha > 0.f)
	{
		P.Box(FVector2D(0, 0), P.Size, FLinearColor(1.f, 0.86f, 0.62f, 0.55f * FlashAlpha * FlashAlpha), &GSolid, 30);
	}
	if (G->GetFadeAlpha() > 0.002f)
	{
		P.Box(FVector2D(0, 0), P.Size, A(G->GetFadeColor(), G->GetFadeAlpha()), &GSolid, 40);
	}
	return LayerId + 50;
}

void SShadeOverlay::PaintObjective(const FPainter& P, double Now) const
{
	AShadeGameMode* G = GM();
	const FShadeObjective* O = G ? G->GetObjective() : nullptr;
	if (!O)
	{
		return;
	}
	const float S = P.S;
	const float Since = static_cast<float>(Now - G->GetObjectiveChangeTime());
	const float In = ShadeEase::OutBack(ShadeEase::Window(Since, 0.f, 0.55f));
	const float Al = HudAlpha * ShadeEase::Window(Since, 0.f, 0.3f);
	const FSlateFontInfo Head = F(TEXT("Black"), 13, 260, S);
	const FSlateFontInfo Title = F(TEXT("Script"), 30, 0, S);
	const FSlateFontInfo Hint = F(TEXT("Hand"), 19, 0, S);

	const float W = FMath::Max(430.f, static_cast<float>(P.Measure(O->Title.ToString(), Title).X) + 72.f);
	// Wrap hint into lines that fit.
	TArray<FString> Lines;
	{
		TArray<FString> Words;
		O->Hint.ToString().ParseIntoArray(Words, TEXT(" "));
		FString Cur;
		for (const FString& Wd : Words)
		{
			const FString Try = Cur.IsEmpty() ? Wd : Cur + TEXT(" ") + Wd;
			if (P.Measure(Try, Hint).X > W - 64.f && !Cur.IsEmpty())
			{
				Lines.Add(Cur);
				Cur = Wd;
			}
			else
			{
				Cur = Try;
			}
		}
		if (!Cur.IsEmpty()) { Lines.Add(Cur); }
	}
	const float TitleH = P.Measure(O->Title.ToString(), Title).Y;
	const float LineH = P.Measure(TEXT("Ag"), Hint).Y * 0.92f;
	const float HintY = 44.f + TitleH + 2.f;
	const float H = HintY + Lines.Num() * LineH + 26.f;
	const FVector2D Pos(UI::Margin - (1.f - In) * 40.f, UI::Margin);
	P.Tile(Pos, FVector2D(W, H), Al);
	P.Text(TEXT("ON THE WAY HOME"), Pos + FVector2D(32.f, 24.f), Head, A(UI::Terracotta, Al), 0.f, 0.f, 0.f, 5);
	P.Text(O->Title.ToString(), Pos + FVector2D(30.f, 42.f), Title, A(UI::Ink, Al), 0.f, 0.f, 0.f, 5);
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		P.Text(Lines[i], Pos + FVector2D(32.f, HintY + i * LineH), Hint, A(UI::InkSoft, Al), 0.f, 0.f, 0.f, 5);
	}
	// Progress: one painted dot per step.
	const int32 N = G->GetObjectiveCount();
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D C = Pos + FVector2D(W - 34.f - (N - 1 - i) * 16.f, 32.f);
		const bool bDone = i < G->GetObjectiveIndex();
		const bool bCur = i == G->GetObjectiveIndex();
		P.Circle(C, bCur ? 5.5f : 4.5f, A(bDone ? UI::Olive : (bCur ? UI::Terracotta : FLinearColor(0.6f, 0.55f, 0.48f, 1.f)), Al), 5);
	}
	// New objective shimmer.
	if (Since < 1.2f)
	{
		const float Sh = ShadeEase::Bump(Since, 0.f, 1.2f);
		P.Box(Pos - FVector2D(5.f, 5.f), FVector2D(W + 10.f, H + 10.f), A(UI::Lemon, 0.55f * Sh), &GRound14, 0);
	}
}

void SShadeOverlay::PaintMarker(const FPainter& P, double Now) const
{
	AShadeGameMode* G = GM();
	AShadeCharacter* K = Shade();
	FVector Target;
	if (!G || !K || MarkerAlpha < 0.02f || !G->GetObjectiveTargetLocation(Target))
	{
		return;
	}
	const float Dist = FVector::Dist(K->GetActorLocation(), Target);
	const float Al = MarkerAlpha * ShadeEase::Window(Dist, 250.f, 600.f) * 0.85f;
	FVector2D At;
	if (Al < 0.02f || !Project(P, Target + FVector(0, 0, 40.f), At))
	{
		return;
	}
	// A small sun-dot waypoint: hairline ring, warm centre, distance in spaced caps.
	const float Breathe = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Now) * 2.2f);
	P.Ring(At, 11.f + Breathe * 1.5f, A(UI::Cream, 0.75f * Al), 1.6f, 3);
	P.Circle(At, 3.6f, A(UI::Sun, Al), 4, 16);
	const FSlateFontInfo Small = F(TEXT("Bold"), 12, 260, P.S);
	P.Text(FString::Printf(TEXT("%d M"), FMath::RoundToInt(Dist / 100.f)), At + FVector2D(0.f, 19.f), Small, A(UI::Cream, 0.85f * Al), 0.5f, 0.f, 0.7f);
}

void SShadeOverlay::PaintPrompt(const FPainter& P, double Now) const
{
	AShadeInteractable* I = PromptTarget.Get();
	AShadeCharacter* K = Shade();
	if (!I || !K || PromptAlpha < 0.02f)
	{
		return;
	}
	FVector2D At;
	if (!Project(P, I->GetFocusLocation(), At))
	{
		At = FVector2D(P.Size.X * 0.5f, P.Size.Y * 0.7f);
	}
	const float Al = PromptAlpha * HudAlpha;
	const float S = P.S;
	const FSlateFontInfo KeyF = F(TEXT("Black"), 16, 0, S);
	FSlateFontInfo VerbF = F(TEXT("Black"), 15, 300, S);
	FSlateFontInfo LabelF = F(TEXT("SerifBoldItalic"), 21, 0, S);
	for (FSlateFontInfo* Fi : { &VerbF, &LabelF })
	{
		Fi->OutlineSettings.OutlineSize = FMath::Max(1, FMath::RoundToInt(1.2f * S));
		Fi->OutlineSettings.OutlineColor = FLinearColor(0.025f, 0.018f, 0.04f, 0.5f * Al);
	}

	const bool bGrab = K->GetGrabbed() == I;
	const bool bHold = I->IsHoldInteraction();
	FString Verb = (bGrab ? FString(TEXT("Let go")) : I->Verb.ToString()).ToUpper();
	if (bHold) { Verb += TEXT("  ·  HOLD"); }
	const FString Label = I->Label.ToString();
	const FVector2D VerbSz = P.Measure(Verb, VerbF);
	const FVector2D LabelSz = P.Measure(Label, LabelF);
	const float R = 18.f;
	const float W = R * 2.f + 14.f + FMath::Max(VerbSz.X, LabelSz.X);
	const float Rise = (1.f - ShadeEase::OutCubic(PromptAlpha)) * 10.f;
	const FVector2D C(At.X - W * 0.5f + R, At.Y - 58.f - Rise);
	// Soft shade behind so it reads over sunlit walls; no box.
	P.Soft(C - FVector2D(R + 22.f, 30.f), FVector2D(W + 54.f, 64.f), 0.32f * Al, 10);
	P.Circle(C + FVector2D(0.f, 2.f), R + 1.f, A(UI::Black, 0.3f * Al), 3);
	P.Circle(C, R, A(UI::Cream, 0.96f * Al), 4);
	P.Text(KeyLabel(TEXT("E"), TEXT("X")), C + FVector2D(0.f, 0.5f), KeyF, A(UI::Ink, Al), 0.5f, 0.5f, 0.f, 6);
	if (bHold || bGrab)
	{
		const float Prog = I->GetProgress();
		P.Ring(C, R + 6.f, A(UI::Cream, 0.22f * Al), 2.5f, 6);
		P.Arc(C, R + 6.f, 0.f, FMath::Max(Prog, 0.001f), A(UI::Sun, Al), 3.f, 7);
	}
	const float TX = C.X + R + 14.f;
	P.Text(Verb, FVector2D(TX, C.Y - 2.f), VerbF, A(UI::Cream, Al), 0.f, 1.f, 0.6f, 6);
	P.Text(Label, FVector2D(TX, C.Y - 1.f), LabelF, A(UI::Cream, 0.78f * Al), 0.f, 0.f, 0.55f, 6);
}

void SShadeOverlay::PaintHealth(const FPainter& P, double Now) const
{
	AShadeCharacter* K = Shade();
	const float Al = HudAlpha;
	if (!K || Al < 0.01f)
	{
		return;
	}
	const float Hp = FMath::Clamp(HealthShown, 0.f, 1.f);
	const float Ghost = FMath::Clamp(HealthGhost, Hp, 1.f);
	const bool bLow = Hp < 0.3f;
	const float Pulse = bLow ? 0.5f + 0.5f * FMath::Sin(static_cast<float>(Now) * 7.f) : 0.f;
	const float W = 300.f, H = 60.f;
	const FVector2D Pos(UI::Margin, P.Size.Y - UI::Margin - H - 54.f);
	P.Tile(Pos, FVector2D(W, H), 0.96f * Al);
	const FSlateFontInfo Head = F(TEXT("Black"), 11, 260, P.S);
	const FSlateFontInfo Num = F(TEXT("Black"), 11, 120, P.S);
	P.Text(bLow ? TEXT("FADING") : TEXT("SHADOW LEFT"), Pos + FVector2D(26.f, 15.f), Head, A(bLow ? FMath::Lerp(UI::Terracotta, UI::Sun, Pulse) : UI::Terracotta, Al), 0.f, 0.f, 0.f, 5);
	P.Text(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Hp * 100.f)), Pos + FVector2D(W - 26.f, 15.f), Num, A(UI::InkSoft, Al), 1.f, 0.f, 0.f, 5);
	const FVector2D BP = Pos + FVector2D(26.f, 35.f);
	const float BW = W - 52.f, BH = 9.f;
	P.Box(BP, FVector2D(BW, BH), A(UI::Ink, 0.14f * Al), &GRound6, 5);
	if (Ghost > Hp + 0.002f)
	{
		P.Box(BP, FVector2D(BW * Ghost, BH), A(UI::Sun, 0.9f * Al), &GRound6, 6);
	}
	if (Hp > 0.002f)
	{
		const FLinearColor Fill = bLow ? FMath::Lerp(UI::Violet, UI::Terracotta, 0.45f + 0.45f * Pulse) : UI::Violet;
		P.Box(BP, FVector2D(FMath::Max(BW * Hp, BH), BH), A(Fill, Al), &GRound6, 7);
		P.Box(BP + FVector2D(3.f, 1.5f), FVector2D(FMath::Max(BW * Hp - 6.f, 0.f), 2.f), A(UI::Cream, 0.22f * Al), &GRound6, 8);
	}
}

void SShadeOverlay::PaintExposure(const FPainter& P, double Now) const
{
	AShadeCharacter* K = Shade();
	if (!K)
	{
		return;
	}
	const float E = ExposureShown;
	if (E < 0.005f)
	{
		return;
	}
	// Heat haze creeping in from the edges, whiter as it peaks.
	const float Edge = FMath::Pow(E, 1.3f);
	const float D = 320.f * (0.4f + 0.6f * Edge);
	const FLinearColor Hot = FMath::Lerp(FLinearColor(1.f, 0.55f, 0.15f, 0.5f * Edge), FLinearColor(1.f, 0.85f, 0.6f, 0.6f * Edge), Edge);
	FLinearColor Clear = Hot;
	Clear.A = 0.f;
	P.Gradient(FVector2D(0, 0), FVector2D(P.Size.X, D), Hot, Clear, Orient_Horizontal, 8);
	P.Gradient(FVector2D(0, P.Size.Y - D), FVector2D(P.Size.X, D), Clear, Hot, Orient_Horizontal, 8);
	P.Gradient(FVector2D(0, 0), FVector2D(D, P.Size.Y), Hot, Clear, Orient_Vertical, 8);
	P.Gradient(FVector2D(P.Size.X - D, 0), FVector2D(D, P.Size.Y), Clear, Hot, Orient_Vertical, 8);

	// A burning ring under Shade: hairline track, sun-coloured fill, a small sun at the centre.
	const FVector2D C(P.Size.X * 0.5f, P.Size.Y - 160.f);
	const float Al = FMath::Clamp(E * 4.f, 0.f, 1.f);
	const float Shake = E > 0.6f ? FMath::Sin(static_cast<float>(Now) * 60.f) * 2.f * E : 0.f;
	const FVector2D CC = C + FVector2D(Shake, 0.f);
	P.Soft(CC - FVector2D(70.f, 60.f), FVector2D(140.f, 150.f), 0.35f * Al, 10);
	P.Ring(CC, 30.f, A(UI::Cream, 0.3f * Al), 2.f, 8);
	P.Arc(CC, 30.f, 0.f, FMath::Max(E, 0.001f), A(FMath::Lerp(UI::Lemon, UI::Sun, E), Al), 4.f, 9);
	P.SunGlyph(CC, 9.f, E, Al, static_cast<float>(Now) * 0.8f);
	const FSlateFontInfo Warn = F(TEXT("Black"), 13, 480, P.S);
	P.Text(E > 0.6f ? TEXT("FIND SHADE") : TEXT("TOO BRIGHT"), CC + FVector2D(0.f, 44.f), Warn, A(UI::Cream, Al), 0.5f, 0.f, 0.7f, 9);
}

void SShadeOverlay::PaintLine(const FPainter& P, double Now) const
{
	AShadeGameMode* G = GM();
	if (!G || G->GetLine().IsEmpty())
	{
		return;
	}
	const float T = static_cast<float>(Now - G->GetLineStartTime());
	const float Hold = G->GetLineHold();
	const float Al = ShadeEase::Window(T, 0.f, 0.5f) * (1.f - ShadeEase::Window(T, Hold, Hold + 0.7f));
	if (Al <= 0.01f)
	{
		return;
	}
	// Storybook narration: an italic serif, centred low, on a soft band of shade.
	FSlateFontInfo Font = F(TEXT("SerifBoldItalic"), 36, 0, P.S);
	Font.OutlineSettings.OutlineSize = FMath::Max(1, FMath::RoundToInt(1.6f * P.S));
	Font.OutlineSettings.OutlineColor = FLinearColor(0.025f, 0.018f, 0.04f, 0.55f * Al);
	const FString Str = G->GetLine().ToString();
	const FVector2D Sz = P.Measure(Str, Font);
	const float LB = ShadeEase::InOutCubic(G->GetLetterbox()) * 0.1f * P.Size.Y;
	const float Y = P.Size.Y - 116.f - FMath::Max(0.f, LB - 40.f) - (1.f - ShadeEase::OutCubic(ShadeEase::Window(T, 0.f, 0.6f))) * 6.f;
	P.Soft(FVector2D(P.Size.X * 0.5f - Sz.X * 0.5f - 90.f, Y - 26.f), FVector2D(Sz.X + 180.f, Sz.Y + 52.f), 0.62f * Al, 12);
	P.Display(Str, FVector2D(P.Size.X * 0.5f, Y), Font, A(UI::Cream, Al), 0.5f, 0.f, 0.6f, 12);
}

void SShadeOverlay::PaintToasts(const FPainter& P, double Now) const
{
	AShadeGameMode* G = GM();
	if (!G)
	{
		return;
	}
	const FSlateFontInfo Kick = F(TEXT("Black"), 12, 520, P.S);
	const FSlateFontInfo Font = F(TEXT("SerifBoldItalic"), 34, 0, P.S);
	float Y = 70.f;
	for (const FShadeToast& T : G->GetToasts())
	{
		const float Age = static_cast<float>(Now - T.StartTime);
		const float Al = ShadeEase::Window(Age, 0.f, 0.35f) * (1.f - ShadeEase::Window(Age, 3.2f, 4.f)) * HudAlpha;
		if (Al <= 0.01f)
		{
			continue;
		}
		const float Draw = ShadeEase::OutCubic(ShadeEase::Window(Age, 0.1f, 0.9f));
		const FString Str = T.Text.ToString();
		const FVector2D Sz = P.Measure(Str, Font);
		const float Cx = P.Size.X * 0.5f;
		P.Soft(FVector2D(Cx - Sz.X * 0.5f - 160.f, Y - 18.f), FVector2D(Sz.X + 320.f, 96.f), 0.3f * Al, 10);
		P.Text(TEXT("SHADE MADE"), FVector2D(Cx, Y), Kick, A(UI::Sun, Al), 0.5f, 0.f, 0.6f, 6);
		P.Display(Str, FVector2D(Cx, Y + 18.f), Font, A(UI::Cream, Al), 0.5f, 0.f, 0.5f, 6);
		const float RW = 110.f * Draw;
		const float RY = Y + 18.f + Sz.Y * 0.55f;
		P.Rule(FVector2D(Cx - Sz.X * 0.5f - 24.f - RW, RY), RW, A(UI::Cream, 0.6f * Al));
		P.Rule(FVector2D(Cx + Sz.X * 0.5f + 24.f, RY), RW, A(UI::Cream, 0.6f * Al));
		Y += 104.f;
	}
}

void SShadeOverlay::PaintControls(const FPainter& P, float Alpha, const FVector2D& Origin) const
{
	if (Alpha <= 0.01f)
	{
		return;
	}
	const FSlateFontInfo KeyF = F(TEXT("Bold"), 12, 60, P.S);
	const FSlateFontInfo Lab = F(TEXT("Bold"), 12, 300, P.S);
	struct FRow { const TCHAR* K; const TCHAR* Pad; const TCHAR* Label; };
	const FRow Rows[] = { { TEXT("WASD"), TEXT("LS"), TEXT("MOVE") }, { TEXT("MOUSE"), TEXT("RS"), TEXT("LOOK") }, { TEXT("SPACE"), TEXT("A"), TEXT("JUMP") },
		{ TEXT("SHIFT"), TEXT("L3"), TEXT("TROT") }, { TEXT("E"), TEXT("X"), TEXT("USE") } };
	float X = Origin.X;
	for (const FRow& R : Rows)
	{
		const float W = P.Key(KeyLabel(R.K, R.Pad), FVector2D(X, Origin.Y), KeyF, Alpha, 26.f);
		const FVector2D Sz = P.Text(R.Label, FVector2D(X + W + 9.f, Origin.Y + 13.f), Lab, A(UI::Cream, 0.85f * Alpha), 0.f, 0.5f, 0.6f);
		X += W + Sz.X + 34.f;
	}
}

void SShadeOverlay::PaintTitle(const FPainter& P, double Now) const
{
	AShadeHUD* H = Owner.Get();
	const float T = static_cast<float>(Now - H->GetMenuOpenTime());
	const float S = P.S;
	auto Stage = [T](float A0, float B0) { return ShadeEase::OutCubic(ShadeEase::Window(T, A0, B0)); };
	// The type sits in shade, like its hero: a soft wash darkens the left of the frame and the very bottom.
	const float Bg = Stage(0.f, 1.8f);
	P.Gradient(FVector2D(0, 0), FVector2D(P.Size.X * 0.6f, P.Size.Y), FLinearColor(0.02f, 0.02f, 0.045f, 0.66f * Bg), FLinearColor(0.02f, 0.02f, 0.045f, 0.f), Orient_Vertical, 1);
	P.Gradient(FVector2D(0, P.Size.Y - 240.f), FVector2D(P.Size.X, 240.f), FLinearColor(0.02f, 0.02f, 0.045f, 0.f), FLinearColor(0.02f, 0.02f, 0.045f, 0.55f * Bg), Orient_Horizontal, 1);

	const float X0 = 150.f;
	const FSlateFontInfo Kicker = F(TEXT("Black"), 13, 640, S);
	const FSlateFontInfo Word = F(TEXT("SerifBold"), 150, 0, S);
	const FSlateFontInfo Amp = F(TEXT("SerifBoldItalic"), 128, 0, S);
	const FSlateFontInfo TagF = F(TEXT("SerifItalic"), 33, 0, S);
	const FSlateFontInfo Foot = F(TEXT("Bold"), 11, 420, S);

	const float K = Stage(0.4f, 1.4f);
	P.Text(TEXT("A  STORY  FROM  VELLARA"), FVector2D(X0 + 6.f, 236.f + (1.f - K) * 10.f), Kicker, A(UI::Sun, 0.95f * K), 0.f, 0.f, 0.5f, 6);
	const float W1 = Stage(0.7f, 2.0f);
	const FVector2D ShadeSz = P.Measure(TEXT("Shade"), Word);
	const FVector2D L1(X0, 250.f + (1.f - W1) * 22.f);
	P.Display(TEXT("Shade"), L1, Word, A(UI::Cream, W1), 0.f, 0.f, 0.7f);
	const float W2 = Stage(1.0f, 2.3f);
	const FVector2D AmpSz = P.Measure(TEXT("&"), Amp);
	const FVector2D L2(X0 + 96.f, L1.Y + ShadeSz.Y * 0.72f + (1.f - W2) * 16.f);
	P.Display(TEXT("&"), L2 + FVector2D(0.f, ShadeSz.Y * 0.09f), Amp, A(UI::Sun, W2), 0.f, 0.f, 0.6f);
	P.Display(TEXT("Seek"), L2 + FVector2D(AmpSz.X + 16.f, 0.f), Word, A(UI::Cream, W2), 0.f, 0.f, 0.7f);
	const float R = Stage(1.7f, 2.8f);
	const float RuleY = L2.Y + ShadeSz.Y * 0.93f;
	P.Rule(FVector2D(X0 + 6.f, RuleY), 380.f * R, A(UI::Sun, 0.8f * R), 1.6f);
	P.Circle(FVector2D(X0 + 6.f + 380.f * R + 10.f, RuleY + 0.8f), 3.f, A(UI::Sun, 0.9f * R), 4, 12);
	P.Display(TEXT("a little shadow's long way home"), FVector2D(X0 + 6.f, RuleY + 16.f), TagF, A(UI::Cream, 0.9f * Stage(1.9f, 3.0f)), 0.f, 0.f, 0.45f);

	PaintMenuItems(P, Now, FVector2D(X0 + 30.f, RuleY + 140.f), Stage(2.3f, 3.2f), false);
	PaintControls(P, 0.8f * Stage(2.7f, 3.6f), FVector2D(X0 + 6.f, P.Size.Y - 96.f));
	P.Text(TEXT("VELLARA  ·  LATE AFTERNOON"), FVector2D(P.Size.X - 90.f, P.Size.Y - 83.f), Foot, A(UI::Cream, 0.6f * Stage(2.7f, 3.6f)), 1.f, 0.5f, 0.6f, 6);
}

void SShadeOverlay::PaintMenuItems(const FPainter& P, double Now, const FVector2D& Origin, float Alpha, bool bCentered) const
{
	AShadeHUD* H = Owner.Get();
	if (Alpha <= 0.01f)
	{
		return;
	}
	const FSlateFontInfo Item = F(TEXT("SerifBold"), 38, 0, P.S);
	const int32 N = H->GetMenuItemCount();
	const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Now) * 2.6f);
	for (int32 i = 0; i < N; ++i)
	{
		const FString Str = H->GetMenuItemLabel(i).ToString();
		const bool bSel = i == H->Selected;
		const FVector2D Sz = P.Measure(Str, Item);
		const float Y = Origin.Y + i * 60.f;
		const float X = bCentered ? Origin.X - Sz.X * 0.5f : Origin.X + (bSel ? 14.f : 0.f);
		if (bSel)
		{
			const FVector2D Dot(X - 24.f, Y + Sz.Y * 0.56f);
			P.Ring(Dot, 8.f + Pulse * 1.5f, A(UI::Sun, 0.55f * Alpha), 1.4f, 7);
			P.Circle(Dot, 3.6f, A(UI::Sun, Alpha), 8, 14);
			if (bCentered)
			{
				const FVector2D Dot2(X + Sz.X + 24.f, Dot.Y);
				P.Ring(Dot2, 8.f + Pulse * 1.5f, A(UI::Sun, 0.55f * Alpha), 1.4f, 7);
				P.Circle(Dot2, 3.6f, A(UI::Sun, Alpha), 8, 14);
			}
		}
		P.Display(Str, FVector2D(X, Y), Item, A(UI::Cream, Alpha * (bSel ? 1.f : 0.48f)), 0.f, 0.f, 0.5f, 8);
		ItemRects.Add(FBox2D(FVector2D(X - 44.f, Y), FVector2D(X + Sz.X + 44.f, Y + Sz.Y)));
	}
}

void SShadeOverlay::PaintPause(const FPainter& P, double Now) const
{
	AShadeHUD* H = Owner.Get();
	const float T = static_cast<float>(Now - H->GetMenuOpenTime());
	const float Al = ShadeEase::OutCubic(ShadeEase::Window(T, 0.f, 0.3f));
	P.Box(FVector2D(0, 0), P.Size, FLinearColor(0.02f, 0.02f, 0.05f, 0.42f * Al), &GSolid, 5);
	P.Gradient(FVector2D(0, 0), FVector2D(P.Size.X * 0.55f, P.Size.Y), FLinearColor(0.02f, 0.02f, 0.045f, 0.5f * Al), FLinearColor(0.02f, 0.02f, 0.045f, 0.f), Orient_Vertical, 5);
	const float X0 = 150.f;
	const FSlateFontInfo Kicker = F(TEXT("Black"), 13, 640, P.S);
	const FSlateFontInfo Head = F(TEXT("SerifBoldItalic"), 120, 0, P.S);
	P.Text(TEXT("PAUSED  ·  A QUIET MOMENT"), FVector2D(X0 + 6.f, 286.f), Kicker, A(UI::Sun, Al), 0.f, 0.f, 0.5f, 7);
	const FVector2D HS = P.Display(TEXT("Siesta"), FVector2D(X0, 300.f - (1.f - Al) * 10.f), Head, A(UI::Cream, Al), 0.f, 0.f, 0.6f, 7);
	P.Rule(FVector2D(X0 + 6.f, 300.f + HS.Y + 6.f), 320.f * Al, A(UI::Sun, 0.8f * Al), 1.6f, 7);
	PaintMenuItems(P, Now, FVector2D(X0 + 30.f, 300.f + HS.Y + 60.f), Al, false);

	// Controls reference, right column.
	const FSlateFontInfo KeyF = F(TEXT("Bold"), 12, 60, P.S);
	const FSlateFontInfo Lab = F(TEXT("SerifItalic"), 24, 0, P.S);
	struct FRow { const TCHAR* K; const TCHAR* Pad; const TCHAR* Label; };
	const FRow Rows[] = { { TEXT("WASD"), TEXT("LS"), TEXT("Pad through the shade") }, { TEXT("MOUSE"), TEXT("RS"), TEXT("Look around") },
		{ TEXT("SPACE"), TEXT("A"), TEXT("Spring up") }, { TEXT("SHIFT"), TEXT("L3"), TEXT("Trot") },
		{ TEXT("E"), TEXT("X"), TEXT("Push, tug, nudge, wake") }, { TEXT("ESC"), TEXT("START"), TEXT("Siesta") } };
	const float RX = P.Size.X - 560.f;
	float Y = 330.f;
	P.Text(TEXT("PAWS & PADS"), FVector2D(RX, Y - 44.f), Kicker, A(UI::Sun, 0.9f * Al), 0.f, 0.f, 0.5f, 7);
	for (const FRow& Row : Rows)
	{
		const float W = P.Key(KeyLabel(Row.K, Row.Pad), FVector2D(RX, Y), KeyF, Al, 28.f);
		P.Text(Row.Label, FVector2D(RX + FMath::Max(W, 84.f) + 18.f, Y + 14.f), Lab, A(UI::Cream, 0.9f * Al), 0.f, 0.5f, 0.5f, 7);
		Y += 52.f;
	}
}

void SShadeOverlay::PaintEnd(const FPainter& P, double Now) const
{
	AShadeHUD* H = Owner.Get();
	AShadeGameMode* G = GM();
	AShadeCharacter* K = Shade();
	const float T = static_cast<float>(Now - H->GetMenuOpenTime());
	const float S = P.S;
	auto Stage = [T](float A0, float B0) { return ShadeEase::OutCubic(ShadeEase::Window(T, A0, B0)); };
	const float In = Stage(0.3f, 1.8f);
	P.Box(FVector2D(0, 0), P.Size, FLinearColor(0.03f, 0.02f, 0.05f, 0.28f * In), &GSolid, 1);
	P.Gradient(FVector2D(0, P.Size.Y * 0.2f), FVector2D(P.Size.X, P.Size.Y * 0.8f), FLinearColor(0.03f, 0.02f, 0.05f, 0.f), FLinearColor(0.03f, 0.02f, 0.05f, 0.75f * In), Orient_Horizontal, 1);
	const FSlateFontInfo Kicker = F(TEXT("Black"), 13, 640, S);
	const FSlateFontInfo Logo = F(TEXT("SerifBoldItalic"), 156, 0, S);
	const FSlateFontInfo Sub = F(TEXT("SerifBoldItalic"), 32, 0, S);
	const FSlateFontInfo StatN = F(TEXT("SerifBold"), 48, 0, S);
	const FSlateFontInfo StatL = F(TEXT("Black"), 11, 440, S);
	const float Cx = P.Size.X * 0.5f;
	const float Y0 = P.Size.Y * 0.3f - (1.f - In) * 18.f;
	P.Text(TEXT("HOME  BEFORE  SUPPER"), FVector2D(Cx, Y0), Kicker, A(UI::Sun, In), 0.5f, 0.f, 0.5f, 6);
	const FVector2D LS = P.Display(TEXT("Reunited"), FVector2D(Cx, Y0 + 16.f), Logo, A(UI::Cream, In), 0.5f, 0.f, 0.7f);
	const float R = Stage(1.0f, 2.0f);
	const float RuleY = Y0 + 16.f + LS.Y * 0.98f;
	P.Rule(FVector2D(Cx - 220.f * R, RuleY), 440.f * R, A(UI::Sun, 0.8f * R), 1.6f);
	const FVector2D SubSz = P.Measure(TEXT("Biscotto has his shadow back, just in time for supper."), Sub);
	P.Soft(FVector2D(Cx - SubSz.X * 0.5f - 70.f, RuleY + 2.f), FVector2D(SubSz.X + 140.f, SubSz.Y + 34.f), 0.5f * Stage(1.2f, 2.2f), 10);
	P.Display(TEXT("Biscotto has his shadow back, just in time for supper."), FVector2D(Cx, RuleY + 18.f), Sub, A(UI::Cream, 0.95f * Stage(1.2f, 2.2f)), 0.5f, 0.f, 0.6f);

	const float SIn = Stage(1.8f, 2.6f);
	const FString Stats[3][2] = {
		{ G ? FormatTime(G->GetRunTime()) : TEXT("-"), TEXT("TIME") },
		{ FString::FromInt(G ? G->GetPuzzlesSolved() : 0) + TEXT(" / 3"), TEXT("SHADE MADE") },
		{ FString::FromInt(K ? K->GetEvaporations() : 0), TEXT("EVAPORATIONS") } };
	const float SY = RuleY + 110.f;
	for (int32 i = 0; i < 3; ++i)
	{
		const FVector2D SC(Cx + (i - 1) * 240.f, SY);
		P.Display(Stats[i][0], SC, StatN, A(UI::Cream, SIn), 0.5f, 0.f, 0.5f);
		P.Text(Stats[i][1], SC + FVector2D(0.f, 72.f), StatL, A(UI::Sun, SIn), 0.5f, 0.f, 0.5f, 6);
		if (i > 0)
		{
			P.Box(FVector2D(SC.X - 120.f, SY + 10.f), FVector2D(1.2f, 66.f), A(UI::Cream, 0.3f * SIn), &GSolid, 6);
		}
	}
	PaintMenuItems(P, Now, FVector2D(Cx, SY + 130.f), Stage(2.4f, 3.0f), true);
}

FReply SShadeOverlay::OnKeyDown(const FGeometry& Geo, const FKeyEvent& Key)
{
	AShadeHUD* H = Owner.Get();
	AShadeGameMode* G = GM();
	if (!H || !G || (PC() && PC()->IsAutoplaying()))
	{
		return FReply::Unhandled();
	}
	const FKey K = Key.GetKey();
	if (H->GetMenu() == EShadeMenu::Title && K == EKeys::Gamepad_FaceButton_Left)
	{
		H->ActivateMenuItem(H->Selected);
		return FReply::Handled();
	}
	const int32 N = H->GetMenuItemCount();
	if (N > 0)
	{
		if (K == EKeys::Up || K == EKeys::W || K == EKeys::Gamepad_DPad_Up)
		{
			H->Selected = (H->Selected + N - 1) % N;
			ShadeAssets::Play2D(H, TEXT("SFX_UI_Move"), 0.5f);
			return FReply::Handled();
		}
		if (K == EKeys::Down || K == EKeys::S || K == EKeys::Gamepad_DPad_Down)
		{
			H->Selected = (H->Selected + 1) % N;
			ShadeAssets::Play2D(H, TEXT("SFX_UI_Move"), 0.5f);
			return FReply::Handled();
		}
		if (K == EKeys::Enter || K == EKeys::E || K == EKeys::SpaceBar || K == EKeys::Gamepad_FaceButton_Bottom)
		{
			H->ActivateMenuItem(H->Selected);
			return FReply::Handled();
		}
	}
	if ((K == EKeys::Escape || K == EKeys::P || K == EKeys::Gamepad_Special_Right) && H->GetMenu() == EShadeMenu::Pause)
	{
		if (AShadePlayerController* C = PC()) { C->ClosePauseMenu(); }
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SShadeOverlay::OnMouseMove(const FGeometry& Geo, const FPointerEvent& Mouse)
{
	AShadeHUD* H = Owner.Get();
	if (!H)
	{
		return FReply::Unhandled();
	}
	const FVector2D Local = Geo.AbsoluteToLocal(Mouse.GetScreenSpacePosition()) / LastS;
	for (int32 i = 0; i < ItemRects.Num(); ++i)
	{
		if (ItemRects[i].IsInside(Local) && H->Selected != i)
		{
			H->Selected = i;
			ShadeAssets::Play2D(H, TEXT("SFX_UI_Move"), 0.4f);
		}
	}
	return FReply::Unhandled();
}

FReply SShadeOverlay::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Mouse)
{
	AShadeHUD* H = Owner.Get();
	AShadeGameMode* G = GM();
	if (!H || !G)
	{
		return FReply::Unhandled();
	}
	const FVector2D Local = Geo.AbsoluteToLocal(Mouse.GetScreenSpacePosition()) / LastS;
	for (int32 i = 0; i < ItemRects.Num(); ++i)
	{
		if (ItemRects[i].IsInside(Local))
		{
			H->Selected = i;
			H->ActivateMenuItem(i);
			return FReply::Handled();
		}
	}
	return FReply::Handled();
}

// =====================================================================================================================
// AShadeHUD
// =====================================================================================================================

void AShadeHUD::BeginPlay()
{
	Super::BeginPlay();
	const FString Dir = FPaths::ProjectContentDir() / TEXT("Shade/UI/Fonts/");
	struct FFace { const TCHAR* Name; const TCHAR* File; };
	const FFace Faces[] = {
		{ TEXT("Script"), TEXT("Lobster-Regular.ttf") }, { TEXT("Hand"), TEXT("Kalam-Regular.ttf") },
		{ TEXT("Body"), TEXT("Lato-Regular.ttf") }, { TEXT("Bold"), TEXT("Lato-Bold.ttf") },
		{ TEXT("Black"), TEXT("Lato-Black.ttf") }, { TEXT("Italic"), TEXT("Lato-Italic.ttf") },
		{ TEXT("Serif"), TEXT("CormorantGaramond-Medium.ttf") }, { TEXT("SerifBold"), TEXT("CormorantGaramond-SemiBold.ttf") },
		{ TEXT("SerifItalic"), TEXT("CormorantGaramond-Italic.ttf") }, { TEXT("SerifBoldItalic"), TEXT("CormorantGaramond-SemiBoldItalic.ttf") },
	};
	for (const FFace& Face : Faces)
	{
		const FString Path = Dir + Face.File;
		if (IFileManager::Get().FileExists(*Path))
		{
			TSharedPtr<FStandaloneCompositeFont> C = MakeShared<FStandaloneCompositeFont>();
			C->DefaultTypeface.AppendFont(TEXT("Regular"), Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
			Fonts.Add(Face.Name, C);
		}
		else
		{
			UE_LOG(LogShade, Warning, TEXT("UI font missing: %s"), *Path);
		}
	}

	TWeakObjectPtr<AShadeHUD> Self(this);
	SAssignNew(Root, SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBackgroundBlur)
			.bApplyAlphaToBlur(true)
			.BlurStrength_Lambda([Self]()
			{
				if (!Self.IsValid()) { return 0.f; }
				const double T = FPlatformTime::Seconds() - Self->GetMenuOpenTime();
				return Self->GetMenu() == EShadeMenu::Pause ? 10.f * ShadeEase::OutCubic(static_cast<float>(T / 0.3)) : 0.f;
			})
			.Visibility_Lambda([Self]()
			{
				return (Self.IsValid() && Self->GetMenu() == EShadeMenu::Pause) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
		]
		+ SOverlay::Slot()
		[
			SAssignNew(Overlay, SShadeOverlay)
			.Owner(Self)
			.Visibility_Lambda([Self]()
			{
				return (Self.IsValid() && Self->GetMenu() != EShadeMenu::None) ? EVisibility::Visible : EVisibility::HitTestInvisible;
			})
		];
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->AddViewportWidgetContent(Root.ToSharedRef(), 50);
	}
	if (AShadeGameMode* GM = GetWorld()->GetAuthGameMode<AShadeGameMode>())
	{
		if (AShadePlayerController* P = Cast<AShadePlayerController>(GetOwningPlayerController()))
		{
			P->OnPhaseChanged(GM->GetPhase());
		}
	}
}

void AShadeHUD::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GEngine && GEngine->GameViewport && Root.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Root.ToSharedRef());
	}
	Root.Reset();
	Overlay.Reset();
	Super::EndPlay(Reason);
}

TSharedPtr<SWidget> AShadeHUD::GetFocusWidget() const
{
	return Overlay;
}

FSlateFontInfo AShadeHUD::Font(FName Face, float Size, int32 LetterSpacing) const
{
	const float Px = FMath::Max(1.f, Size);
	FSlateFontInfo Info;
	if (const TSharedPtr<FStandaloneCompositeFont>* C = Fonts.Find(Face))
	{
		Info = FSlateFontInfo(StaticCastSharedPtr<const FCompositeFont>(*C), Px, TEXT("Regular"));
	}
	else
	{
		Info = FCoreStyle::GetDefaultFontStyle(Face == TEXT("Black") || Face == TEXT("Bold") ? TEXT("Bold") : TEXT("Regular"), FMath::RoundToInt(Px));
	}
	Info.LetterSpacing = LetterSpacing;
	return Info;
}

void AShadeHUD::ShowMenu(EShadeMenu NewMenu)
{
	if (Menu == NewMenu)
	{
		return;
	}
	Menu = NewMenu;
	MenuOpenTime = FPlatformTime::Seconds();
	Selected = 0;
	if (Menu != EShadeMenu::None && Overlay.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(Overlay, EFocusCause::SetDirectly);
	}
}

int32 AShadeHUD::GetMenuItemCount() const
{
	switch (Menu)
	{
	case EShadeMenu::Title: return 2;
	case EShadeMenu::Pause: return 3;
	case EShadeMenu::End: return 2;
	default: return 0;
	}
}

FText AShadeHUD::GetMenuItemLabel(int32 Index) const
{
	if (Menu == EShadeMenu::Title)
	{
		const FText L[2] = { LOCTEXT("Begin", "Begin the walk home"), LOCTEXT("QuitTitle", "Close the shutters") };
		return L[FMath::Clamp(Index, 0, 1)];
	}
	if (Menu == EShadeMenu::Pause)
	{
		const FText L[3] = { LOCTEXT("Resume", "Back to the sun"), LOCTEXT("Restart", "Start over"), LOCTEXT("Quit", "Close the shutters") };
		return L[FMath::Clamp(Index, 0, 2)];
	}
	const FText L[2] = { LOCTEXT("Again", "Wander again"), LOCTEXT("QuitEnd", "Close the shutters") };
	return L[FMath::Clamp(Index, 0, 1)];
}

void AShadeHUD::ActivateMenuItem(int32 Index)
{
	AShadeGameMode* GM = GetWorld()->GetAuthGameMode<AShadeGameMode>();
	AShadePlayerController* P = Cast<AShadePlayerController>(GetOwningPlayerController());
	if (!GM || !P)
	{
		return;
	}
	ShadeAssets::Play2D(this, TEXT("SFX_UI_Select"), 0.7f);
	if (Menu == EShadeMenu::Title)
	{
		if (Index == 0) { GM->StartGame(); }
		else { GM->QuitGame(); }
	}
	else if (Menu == EShadeMenu::Pause)
	{
		if (Index == 0) { P->ClosePauseMenu(); }
		else if (Index == 1) { P->ClosePauseMenu(); GM->RestartRun(); }
		else { GM->QuitGame(); }
	}
	else if (Menu == EShadeMenu::End)
	{
		if (Index == 0) { GM->RestartRun(); }
		else { GM->QuitGame(); }
	}
}

#undef LOCTEXT_NAMESPACE
