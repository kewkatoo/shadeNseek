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
#include "Engine/Texture2D.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "ShadeHUD"

// =====================================================================================================================
// Design tokens (Docs/UPGRADE_PLAN.md, "3. UI"): sun-bleached Mediterranean - cream paper, ink, shade violet,
// terracotta, sun. The palette is authored as sRGB hex and converted to linear once, so Slate shows the real colours.
// =====================================================================================================================
namespace UI
{
	inline FLinearColor SRGB(uint32 Hex)
	{
		return FLinearColor::FromSRGBColor(FColor(static_cast<uint8>((Hex >> 16) & 0xFF), static_cast<uint8>((Hex >> 8) & 0xFF), static_cast<uint8>(Hex & 0xFF), 255));
	}

	const FLinearColor Ink = SRGB(0x2B2230);        // text on paper
	const FLinearColor Shade = SRGB(0x3A2F45);      // shade cards, the shadow meter fill
	const FLinearColor Cream = SRGB(0xF6EBD9);      // paper cards, keycaps, text on shade
	const FLinearColor Plaster = SRGB(0xE6D3B3);    // paper hairline, keycap lip
	const FLinearColor Terracotta = SRGB(0xA84A2A); // kickers on paper, glaze line, current step
	const FLinearColor Sun = SRGB(0xF2A541);        // accents on dark only: kickers, rules, underline, hold ring
	const FLinearColor Sea = SRGB(0x2F6F8F);        // tile corner diamonds
	const FLinearColor Olive = SRGB(0x6E7B3A);      // finished steps
	const FLinearColor InkSoft = SRGB(0x5A4E5C);    // hints and secondary text on paper
	const FLinearColor Night = SRGB(0x120E16);      // derived from Ink: drop shadows, text shadows, dims, letterbox
	constexpr float Margin = 56.f;
	constexpr float StripH = 50.f;                  // controls strip height

	/** Type scale at 1080p (Slate font size; F() scales it with the viewport height). Nothing below 14. */
	namespace Type
	{
		constexpr float Logo = 128.f;
		constexpr float Heading = 96.f;
		constexpr float Menu = 34.f;
		constexpr float Title = 28.f;               // card titles, toasts, prompt label
		constexpr float Narration = 26.f;
		constexpr float Body = 18.f;                // hints, labels
		constexpr float Kicker = 14.f;              // caps, tracked
		constexpr float Key = 15.f;
		constexpr int32 Tracking = 120;             // kicker letter spacing, 1/1000 em
		constexpr float NarrationWrap = 1100.f;
	}

	/** Motion: in 0.22 s OutCubic with an 8 px rise, out 0.16 s; menu rows stagger 60 ms; toasts drop in, hold, fade. */
	namespace Motion
	{
		constexpr float In = 0.22f;
		constexpr float Out = 0.16f;
		constexpr float Rise = 8.f;
		constexpr float Stagger = 0.06f;
		constexpr float ToastIn = 0.25f;
		constexpr float ToastHold = 2.8f;
		constexpr float ToastOut = 0.4f;
	}
}

namespace
{
	const FSlateColorBrush GSolid(FLinearColor::White);

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
		const TMap<FName, FSlateBrush>* Glyphs = nullptr;  // key glyph brushes owned by the HUD (may be null)

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

		/** Rounded box; radius and hairline scale with the UI. The brush is copied into the draw element, so a local is fine. */
		void RBox(const FVector2D& Pos, const FVector2D& Sz, const FLinearColor& Fill, float Radius, int32 L = 0, const FLinearColor& Line = FLinearColor::Transparent, float LineW = 0.f) const
		{
			if ((Fill.A <= 0.002f && (Line.A <= 0.002f || LineW <= 0.f)) || Sz.X <= 0.f || Sz.Y <= 0.f) { return; }
			const float R = FMath::Min(Radius, 0.5f * static_cast<float>(FMath::Min(Sz.X, Sz.Y)));
			const FSlateRoundedBoxBrush Brush(FLinearColor::White, R * S, Line, LineW * S);
			FSlateDrawElement::MakeBox(Out, Layer + L, PG(Pos, Sz), &Brush, ESlateDrawEffect::None, Fill);
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

		/** Soft drop shadow: two offset rounded boxes, a wide faint one under a tighter, darker one. */
		void DropShadow(const FVector2D& Pos, const FVector2D& Sz, float Radius, float Alpha, int32 L) const
		{
			RBox(Pos + FVector2D(-3.f, 3.f), Sz + FVector2D(6.f, 8.f), A(UI::Night, 0.12f * Alpha), Radius + 4.f, L);
			RBox(Pos + FVector2D(0.f, 2.f), Sz + FVector2D(0.f, 2.f), A(UI::Night, 0.2f * Alpha), Radius + 1.f, L);
		}

		/**
		 * Paper card (objective, shadow meter): cream, radius 16, plaster hairline, plus the glazed-tile details of the
		 * original objective card - an inset terracotta glaze line and painted corner diamonds. Uses layers L..L+4.
		 */
		void PaperCard(const FVector2D& Pos, const FVector2D& Sz, float Alpha, int32 L) const
		{
			DropShadow(Pos, Sz, 16.f, Alpha, L);
			RBox(Pos, Sz, A(UI::Cream, 0.98f * Alpha), 16.f, L + 1, A(UI::Plaster, Alpha), 1.5f);
			RBox(Pos + FVector2D(6.f, 6.f), Sz - FVector2D(12.f, 12.f), FLinearColor::Transparent, 11.f, L + 2, A(UI::Terracotta, 0.6f * Alpha), 1.25f);
			for (const FVector2D& Corner : { Pos + FVector2D(13, 13), Pos + FVector2D(Sz.X - 13, 13), Pos + FVector2D(13, Sz.Y - 13), Pos + Sz - FVector2D(13, 13) })
			{
				Diamond(Corner, 4.5f, A(UI::Sea, 0.85f * Alpha), L + 3);
				Diamond(Corner, 2.f, A(UI::Sun, Alpha), L + 4);
			}
		}

		/** Shade card (everything floating over the world): Shade at 82%, radius 14, faint cream hairline. Uses layers L..L+1. */
		void ShadeCard(const FVector2D& Pos, const FVector2D& Sz, float Alpha, int32 L) const
		{
			DropShadow(Pos, Sz, 14.f, 0.8f * Alpha, L);
			// The hairline replaces the fill under it, so it is the card colour lifted toward cream, at the card's alpha.
			RBox(Pos, Sz, A(UI::Shade, 0.82f * Alpha), 14.f, L + 1, A(FMath::Lerp(UI::Shade, UI::Cream, 0.2f), 0.82f * Alpha), 1.f);
		}

		FVector2D Measure(const FString& Str, const FSlateFontInfo& Font) const
		{
			const TSharedRef<FSlateFontMeasure> M = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			return M->Measure(Str, Font) / S;
		}

		float Width(const FString& Str, const FSlateFontInfo& Font) const
		{
			return static_cast<float>(Measure(Str, Font).X);
		}

		float LineHeight(const FSlateFontInfo& Font) const
		{
			return static_cast<float>(Measure(TEXT("Ag"), Font).Y);
		}

		/** Trims Str to fit MaxW, ending in an ellipsis. */
		FString Ellipsize(const FString& Str, const FSlateFontInfo& Font, float MaxW) const
		{
			if (Width(Str, Font) <= MaxW)
			{
				return Str;
			}
			FString Cut = Str;
			while (Cut.Len() > 1 && Width(Cut + TEXT("…"), Font) > MaxW)
			{
				Cut = Cut.LeftChop(1);
			}
			Cut.TrimEndInline();
			return Cut + TEXT("…");
		}

		/** Greedy word wrap to MaxW; if the text needs more than MaxLines, the last line ends in an ellipsis. */
		TArray<FString> Wrap(const FString& Str, const FSlateFontInfo& Font, float MaxW, int32 MaxLines) const
		{
			TArray<FString> Words;
			Str.ParseIntoArrayWS(Words);
			TArray<FString> Result;
			FString Cur;
			for (int32 i = 0; i < Words.Num(); ++i)
			{
				const FString Try = Cur.IsEmpty() ? Words[i] : Cur + TEXT(" ") + Words[i];
				if (Cur.IsEmpty() || Width(Try, Font) <= MaxW)
				{
					Cur = Try;
					continue;
				}
				if (Result.Num() + 1 >= MaxLines)
				{
					FString Rest = Try;
					for (int32 j = i + 1; j < Words.Num(); ++j)
					{
						Rest += TEXT(" ") + Words[j];
					}
					Result.Add(Ellipsize(Rest, Font, MaxW));
					return Result;
				}
				Result.Add(Cur);
				Cur = Words[i];
			}
			if (!Cur.IsEmpty())
			{
				Result.Add(Ellipsize(Cur, Font, MaxW));
			}
			return Result;
		}

		/** Align 0 = left/top, 0.5 = centre, 1 = right/bottom. Text on cards takes no shadow; text over the world can. */
		FVector2D Text(const FString& Str, const FVector2D& Pos, const FSlateFontInfo& Font, const FLinearColor& C, float AX = 0.f, float AY = 0.f, float ShadowA = 0.f, int32 L = 3) const
		{
			const FVector2D Sz = Measure(Str, Font);
			if (C.A > 0.002f && !Str.IsEmpty())
			{
				const FVector2D At(Pos.X - Sz.X * AX, Pos.Y - Sz.Y * AY);
				if (ShadowA > 0.f)
				{
					FSlateDrawElement::MakeText(Out, Layer + L - 1, PG(At + FVector2D(1.5f, 2.f), Sz), Str, Font, ESlateDrawEffect::None, A(UI::Night, ShadowA * C.A));
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

		/** Display type over the world with a soft, low-sun shadow (several faint offset copies read as a blur). */
		FVector2D Display(const FString& Str, const FVector2D& Pos, const FSlateFontInfo& Font, const FLinearColor& C, float AX = 0.f, float AY = 0.f, float ShadowA = 0.5f, int32 L = 6) const
		{
			const FVector2D Sz = Measure(Str, Font);
			if (C.A > 0.002f && !Str.IsEmpty())
			{
				const FVector2D At(Pos.X - Sz.X * AX, Pos.Y - Sz.Y * AY);
				const FVector2D Offs[4] = { FVector2D(1.f, 3.f), FVector2D(3.f, 6.f), FVector2D(5.f, 10.f), FVector2D(8.f, 15.f) };
				for (int32 i = 0; i < 4; ++i)
				{
					FSlateDrawElement::MakeText(Out, Layer + L - 1, PG(At + Offs[i], Sz), Str, Font, ESlateDrawEffect::None, A(UI::Night, ShadowA * C.A * (0.32f - i * 0.06f)));
				}
				FSlateDrawElement::MakeText(Out, Layer + L, PG(At, Sz), Str, Font, ESlateDrawEffect::None, C);
			}
			return Sz;
		}

		const FSlateBrush* GlyphFor(const TCHAR* Label) const
		{
			return Glyphs ? Glyphs->Find(FName(Label)) : nullptr;
		}

		static float GlyphW(const FSlateBrush& B, float H)
		{
			return H * static_cast<float>(B.ImageSize.X / FMath::Max(1.0, B.ImageSize.Y));
		}

		/** WASD as four small keys in a row (0.78 of the cap height), when all four glyphs are loaded. */
		bool WasdGlyphs(const FSlateBrush* (&K)[4]) const
		{
			K[0] = GlyphFor(TEXT("W")); K[1] = GlyphFor(TEXT("A")); K[2] = GlyphFor(TEXT("S")); K[3] = GlyphFor(TEXT("D"));
			return K[0] && K[1] && K[2] && K[3];
		}

		/** Width of the one keycap for Label: square/round keys stay square, longer labels grow. */
		float KeyWidth(const FString& Label, const FSlateFontInfo& Font, float Height, bool bRound) const
		{
			if (!bRound)
			{
				const FSlateBrush* K[4];
				if (Label == TEXT("WASD") && WasdGlyphs(K))
				{
					return GlyphW(*K[0], Height * 0.78f) + GlyphW(*K[1], Height * 0.78f) + GlyphW(*K[2], Height * 0.78f) + GlyphW(*K[3], Height * 0.78f) + 6.f;
				}
				if (const FSlateBrush* B = GlyphFor(*Label))
				{
					return GlyphW(*B, Height);
				}
			}
			return (bRound && Label.Len() <= 1) ? Height : FMath::Max(Height, Width(Label, Font) + 20.f);
		}

		/** A glyph key: cream key whose cut-out letter shows a soft ink shadow underneath. Layers L, L+1. Returns width. */
		float GlyphKey(const FSlateBrush& B, const FVector2D& Pos, float H, float Alpha, int32 L) const
		{
			const FVector2D Sz(GlyphW(B, H), H);
			FSlateDrawElement::MakeBox(Out, Layer + L, PG(Pos + FVector2D(0.f, 2.f), Sz), &B, ESlateDrawEffect::None, A(UI::Night, 0.55f * Alpha));
			FSlateDrawElement::MakeBox(Out, Layer + L + 1, PG(Pos, Sz), &B, ESlateDrawEffect::None, A(UI::Cream, Alpha));
			return static_cast<float>(Sz.X);
		}

		/**
		 * The one keycap: keyboard keys and the mouse use the input glyphs when loaded; otherwise (and for gamepad
		 * buttons) a cream key with an ink label on a plaster lip - rounded squares (radius 6), circles or pills.
		 * Uses layers L..L+2. Returns width.
		 */
		float Key(const FString& Label, const FVector2D& Pos, const FSlateFontInfo& Font, float Alpha, float Height = 32.f, bool bRound = false, int32 L = 4) const
		{
			if (!bRound)
			{
				const FSlateBrush* K[4];
				if (Label == TEXT("WASD") && WasdGlyphs(K))
				{
					const float H = Height * 0.78f;
					float X = 0.f;
					for (int32 i = 0; i < 4; ++i)
					{
						X += GlyphKey(*K[i], Pos + FVector2D(X, (Height - H) * 0.5f), H, Alpha, L) + (i < 3 ? 2.f : 0.f);
					}
					return X;
				}
				if (const FSlateBrush* B = GlyphFor(*Label))
				{
					return GlyphKey(*B, Pos, Height, Alpha, L);
				}
			}
			const float Lip = 3.f;
			const float W = KeyWidth(Label, Font, Height, bRound);
			const float R = bRound ? Height * 0.5f : 6.f;
			RBox(Pos + FVector2D(0.f, 1.5f), FVector2D(W, Height), A(UI::Night, 0.22f * Alpha), R, L);
			RBox(Pos + FVector2D(0.f, Lip), FVector2D(W, Height - Lip), A(UI::Plaster, Alpha), R, L);
			RBox(Pos, FVector2D(W, Height - Lip), A(UI::Cream, Alpha), R, L + 1);
			Text(Label, Pos + FVector2D(W * 0.5f, (Height - Lip) * 0.5f), Font, A(UI::Ink, Alpha), 0.5f, 0.5f, 0.f, L + 2);
			return W;
		}

		/** Tiny painted sun: disc + rays; Fill tints it from pale plaster to blazing. */
		void SunGlyph(const FVector2D& C, float R, float Fill, float Alpha, float Spin, int32 L = 3) const
		{
			const FLinearColor Col = FMath::Lerp(UI::Plaster, UI::Sun, Fill);
			for (int32 i = 0; i < 10; ++i)
			{
				const float Ang = Spin + i * 2.f * PI / 10.f;
				const FVector2D D(FMath::Cos(Ang), FMath::Sin(Ang));
				const FVector2D N(-D.Y, D.X);
				const float L0 = R * 1.25f, L1 = R * (1.65f + 0.25f * Fill);
				Poly({ C + D * L0 + N * R * 0.16f, C + D * L1, C + D * L0 - N * R * 0.16f }, A(Col, Alpha), L);
			}
			Circle(C, R, A(Col, Alpha), L);
			Circle(C, R * 0.62f, A(FMath::Lerp(UI::Cream, UI::Sun, 0.35f * Fill), Alpha), L + 1);
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
	bool UsingPad() const
	{
		const AShadePlayerController* P = PC();
		return P && P->IsUsingGamepad();
	}
	FString KeyLabel(const TCHAR* Kbd, const TCHAR* Pad) const
	{
		return UsingPad() ? FString(Pad) : FString(Kbd);
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
	/** Start: seconds after the menu opened when the first row starts its entrance; later rows follow 60 ms apart. */
	void PaintMenuItems(const FPainter& P, double Now, const FVector2D& Origin, float Alpha, bool bCentered, float Start = 0.f) const;

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
	const bool bMarker = bPlay && !bPrompt;
	// Cards ramp in over UI::Motion::In and out over UI::Motion::Out (linear here, eased where they are painted).
	PromptAlpha = FMath::Clamp(PromptAlpha + (bPrompt ? Dt / UI::Motion::In : -Dt / UI::Motion::Out), 0.f, 1.f);
	MarkerAlpha = FMath::Clamp(MarkerAlpha + (bMarker ? Dt / UI::Motion::In : -Dt / UI::Motion::Out), 0.f, 1.f);
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

// Layers (relative to the widget's): heat haze 0, paper cards 2-8, prompt 5-11, toasts / controls / exposure card 9-13,
// narration 12-15, menus 16-21 (dim, panel, text), letterbox 24, evaporation flash 30, fade 40.
int32 SShadeOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
	FPainter P(Geo, Out, LayerId);
	if (const AShadeHUD* OwnerHUD = Owner.Get())
	{
		P.Glyphs = &OwnerHUD->GetGlyphs();
	}
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
		P.Box(FVector2D(0, 0), FVector2D(P.Size.X, LB), UI::Night, &GSolid, 24);
		P.Box(FVector2D(0, P.Size.Y - LB), FVector2D(P.Size.X, LB), UI::Night, &GSolid, 24);
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
			PaintControls(P, Intro * HudAlpha, FVector2D(UI::Margin, P.Size.Y - UI::Margin - UI::StripH));
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
		P.Box(FVector2D(0, 0), P.Size, A(UI::Cream, 0.55f * FlashAlpha * FlashAlpha), &GSolid, 30);
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
	const float In = ShadeEase::OutCubic(ShadeEase::Window(Since, 0.f, UI::Motion::In));
	const float Al = HudAlpha * In;
	if (Al <= 0.01f)
	{
		return;
	}
	const FSlateFontInfo Kick = F(TEXT("BodyHeavy"), UI::Type::Kicker, UI::Type::Tracking, S);
	const FSlateFontInfo TitleF = F(TEXT("Display"), UI::Type::Title, 0, S);
	const FSlateFontInfo HintF = F(TEXT("Body"), UI::Type::Body, 0, S);

	// The card follows its title between 500 and 620 wide; the title wraps to 2 lines and the hint to 3, so nothing
	// ever runs past the card.
	const FString KickStr = TEXT("ON THE WAY HOME");
	const int32 N = G->GetObjectiveCount();
	const float PadX = 30.f, DotGap = 15.f;
	const float DotsW = FMath::Max(0, N - 1) * DotGap + 12.f;
	const float MinW = FMath::Max(500.f, P.Width(KickStr, Kick) + 28.f + DotsW + 2.f * PadX);
	const float W = FMath::Clamp(P.Width(O->Title.ToString(), TitleF) + 2.f * PadX, MinW, FMath::Max(MinW, 620.f));
	const float TextW = W - 2.f * PadX;
	const TArray<FString> TitleLines = P.Wrap(O->Title.ToString(), TitleF, TextW, 2);
	const TArray<FString> HintLines = P.Wrap(O->Hint.ToString(), HintF, TextW, 3);
	const float KickH = P.LineHeight(Kick);
	const float TitleLH = P.LineHeight(TitleF) * 0.86f;
	const float HintLH = P.LineHeight(HintF) * 0.9f;
	const float KickY = 20.f;
	const float TitleY = KickY + KickH - 2.f;
	const float HintY = TitleY + TitleLines.Num() * TitleLH + 4.f;
	const float Hh = HintY + HintLines.Num() * HintLH + 20.f;
	const FVector2D Pos(UI::Margin, UI::Margin + (1.f - In) * UI::Motion::Rise);
	P.PaperCard(Pos, FVector2D(W, Hh), Al, 2);
	P.Text(KickStr, Pos + FVector2D(PadX, KickY), Kick, A(UI::Terracotta, Al), 0.f, 0.f, 0.f, 6);
	for (int32 i = 0; i < TitleLines.Num(); ++i)
	{
		P.Text(TitleLines[i], Pos + FVector2D(PadX, TitleY + i * TitleLH), TitleF, A(UI::Ink, Al), 0.f, 0.f, 0.f, 6);
	}
	for (int32 i = 0; i < HintLines.Num(); ++i)
	{
		P.Text(HintLines[i], Pos + FVector2D(PadX, HintY + i * HintLH), HintF, A(UI::InkSoft, Al), 0.f, 0.f, 0.f, 6);
	}
	// Progress: one painted dot per step, right-aligned on the kicker line (Nunito caps centre ~48% down the line box).
	const float DotY = KickY + KickH * 0.48f;
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D C = Pos + FVector2D(W - PadX - 6.f - (N - 1 - i) * DotGap, DotY);
		const bool bDone = i < G->GetObjectiveIndex();
		const bool bCur = i == G->GetObjectiveIndex();
		P.Circle(C, bCur ? 5.5f : 4.f, bDone ? A(UI::Olive, Al) : (bCur ? A(UI::Terracotta, Al) : A(UI::InkSoft, 0.3f * Al)), 6);
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
	const float In = ShadeEase::OutCubic(MarkerAlpha);
	const float Al = In * ShadeEase::Window(Dist, 250.f, 600.f);
	FVector2D At;
	if (Al < 0.02f || !Project(P, Target + FVector(0, 0, 40.f), At))
	{
		return;
	}
	At.Y += (1.f - In) * UI::Motion::Rise;
	// A small sun-dot waypoint on a disc of shade; the distance sits on a shade pill below it.
	P.Circle(At, 15.f, A(UI::Shade, 0.72f * Al), 2, 32);
	P.Ring(At, 11.f, A(UI::Cream, 0.9f * Al), 1.6f, 3);
	P.Circle(At, 3.6f, A(UI::Sun, Al), 4, 16);
	const FSlateFontInfo Kick = F(TEXT("BodyHeavy"), UI::Type::Kicker, UI::Type::Tracking, P.S);
	const FString Str = FString::Printf(TEXT("%d M"), FMath::RoundToInt(Dist / 100.f));
	const FVector2D TxtSz = P.Measure(Str, Kick);
	const FVector2D PillSz(TxtSz.X + 26.f, TxtSz.Y + 4.f);
	const FVector2D PillPos(At.X - PillSz.X * 0.5f, At.Y + 22.f);
	P.ShadeCard(PillPos, PillSz, Al, 2);
	P.Text(Str, PillPos + PillSz * 0.5f, Kick, A(UI::Cream, Al), 0.5f, 0.5f, 0.f, 5);
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
	const float In = ShadeEase::OutCubic(PromptAlpha);
	const float Al = In * HudAlpha;
	const float S = P.S;
	const FSlateFontInfo KeyF = F(TEXT("BodyHeavy"), UI::Type::Key, 40, S);
	const FSlateFontInfo VerbF = F(TEXT("BodyHeavy"), UI::Type::Kicker, UI::Type::Tracking, S);
	const FSlateFontInfo LabelF = F(TEXT("Display"), UI::Type::Title, 0, S);

	const bool bGrab = K->GetGrabbed() == I;
	const bool bHold = I->IsHoldInteraction();
	const bool bPad = UsingPad();
	FString Verb = (bGrab ? FString(TEXT("Let go")) : I->Verb.ToString()).ToUpper();
	if (bHold) { Verb += TEXT("  ·  HOLD"); }
	const FString Label = I->Label.ToString();
	const FString Glyph = KeyLabel(TEXT("E"), TEXT("X"));
	const FVector2D VerbSz = P.Measure(Verb, VerbF);
	const FVector2D LabelSz = P.Measure(Label, LabelF);

	// Shade card: keycap (the hold ring is a sun arc around it), then the verb kicker over the label.
	const float KeyH = 34.f, RingR = 25.f, PadL = 14.f, PadR = 24.f, Gap = 14.f, PadY = 12.f;
	const float VerbStep = static_cast<float>(VerbSz.Y) * 0.78f;
	const float TextH = VerbStep + static_cast<float>(LabelSz.Y) * 0.86f;
	const float Hh = FMath::Max(2.f * RingR + 14.f, TextH + 2.f * PadY);
	const float W = PadL + 2.f * RingR + Gap + static_cast<float>(FMath::Max(VerbSz.X, LabelSz.X)) + PadR;
	const float Rise = (1.f - In) * UI::Motion::Rise;
	const float MaxX = static_cast<float>(P.Size.X) - UI::Margin - W;
	const float CardX = FMath::Clamp(static_cast<float>(At.X) - W * 0.5f, UI::Margin, FMath::Max(UI::Margin, MaxX));
	const float CardY = FMath::Max(UI::Margin, static_cast<float>(At.Y) - 30.f - Hh) + Rise;
	const FVector2D Pos(CardX, CardY);
	P.ShadeCard(Pos, FVector2D(W, Hh), Al, 5);

	const FVector2D C = Pos + FVector2D(PadL + RingR, Hh * 0.5f);
	const float KW = P.KeyWidth(Glyph, KeyF, KeyH, bPad);
	P.Key(Glyph, C - FVector2D(KW * 0.5f, KeyH * 0.5f), KeyF, Al, KeyH, bPad, 7);
	if (bHold || bGrab)
	{
		const float Prog = I->GetProgress();
		P.Ring(C, RingR, A(UI::Cream, 0.18f * Al), 2.5f, 10);
		P.Arc(C, RingR, 0.f, FMath::Max(Prog, 0.001f), A(UI::Sun, Al), 3.5f, 11);
	}
	const float TX = C.X + RingR + Gap;
	const float TY = Pos.Y + (Hh - TextH) * 0.5f;
	P.Text(Verb, FVector2D(TX, TY), VerbF, A(UI::Sun, Al), 0.f, 0.f, 0.f, 10);
	P.Text(Label, FVector2D(TX, TY + VerbStep), LabelF, A(UI::Cream, Al), 0.f, 0.f, 0.f, 10);
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
	// The one animation that stays on: the low-meter pulse.
	const float Pulse = bLow ? 0.5f + 0.5f * FMath::Sin(static_cast<float>(Now) * 7.f) : 0.f;
	const FSlateFontInfo Kick = F(TEXT("BodyHeavy"), UI::Type::Kicker, UI::Type::Tracking, P.S);
	const FSlateFontInfo Num = F(TEXT("BodyHeavy"), UI::Type::Kicker, 60, P.S);
	const float KickH = P.LineHeight(Kick);
	const float W = 320.f, PadX = 28.f, KickY = 17.f, BH = 10.f;
	const float BarY = KickY + KickH + 3.f;
	const float Hh = BarY + BH + 20.f;
	// Sits just above the controls strip (which only shows early on), so it never moves.
	const FVector2D Pos(UI::Margin, P.Size.Y - UI::Margin - UI::StripH - 14.f - Hh);
	P.PaperCard(Pos, FVector2D(W, Hh), Al, 2);
	P.Text(bLow ? TEXT("FADING") : TEXT("SHADOW LEFT"), Pos + FVector2D(PadX, KickY), Kick, A(UI::Terracotta, Al * (bLow ? 0.55f + 0.45f * Pulse : 1.f)), 0.f, 0.f, 0.f, 6);
	P.Text(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Hp * 100.f)), Pos + FVector2D(W - PadX, KickY), Num, A(UI::InkSoft, Al), 1.f, 0.f, 0.f, 6);
	const FVector2D BP = Pos + FVector2D(PadX, BarY);
	const float BW = W - 2.f * PadX;
	P.RBox(BP, FVector2D(BW, BH), A(UI::Ink, 0.14f * Al), BH * 0.5f, 5);
	if (Ghost > Hp + 0.002f)
	{
		P.RBox(BP, FVector2D(BW * Ghost, BH), A(UI::Sun, 0.9f * Al), BH * 0.5f, 6);
	}
	if (Hp > 0.002f)
	{
		const FLinearColor Fill = bLow ? FMath::Lerp(UI::Shade, UI::Terracotta, 0.45f + 0.45f * Pulse) : UI::Shade;
		P.RBox(BP, FVector2D(FMath::Max(BW * Hp, BH), BH), A(Fill, Al), BH * 0.5f, 7);
		P.RBox(BP + FVector2D(3.f, 1.5f), FVector2D(FMath::Max(BW * Hp - 6.f, 0.f), 2.f), A(UI::Cream, 0.22f * Al), 1.f, 8);
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
	// Heat haze creeping in from the edges, sun-gold paling to cream as it peaks. It sits under every card.
	const float Edge = FMath::Pow(E, 1.3f);
	const float D = 320.f * (0.4f + 0.6f * Edge);
	const FLinearColor Hot = FMath::Lerp(A(UI::Sun, 0.5f * Edge), A(UI::Cream, 0.6f * Edge), Edge);
	FLinearColor Clear = Hot;
	Clear.A = 0.f;
	P.Gradient(FVector2D(0, 0), FVector2D(P.Size.X, D), Hot, Clear, Orient_Horizontal, 0);
	P.Gradient(FVector2D(0, P.Size.Y - D), FVector2D(P.Size.X, D), Clear, Hot, Orient_Horizontal, 0);
	P.Gradient(FVector2D(0, 0), FVector2D(D, P.Size.Y), Hot, Clear, Orient_Vertical, 0);
	P.Gradient(FVector2D(P.Size.X - D, 0), FVector2D(D, P.Size.Y), Clear, Hot, Orient_Vertical, 0);

	// Warning on a shade card, clear of the haze and above the narration line: burning ring + small sun, then the call.
	const float Al = FMath::Clamp(E * 4.f, 0.f, 1.f);
	const float In = ShadeEase::OutCubic(Al);
	const FSlateFontInfo Warn = F(TEXT("BodyHeavy"), UI::Type::Body, UI::Type::Tracking, P.S);
	const FString Msg = E > 0.6f ? TEXT("FIND SHADE") : TEXT("TOO BRIGHT");
	const float RingR = 22.f, PadL = 16.f, Gap = 16.f, PadR = 28.f, Hh = 72.f;
	const float MsgW = FMath::Max(P.Width(TEXT("FIND SHADE"), Warn), P.Width(TEXT("TOO BRIGHT"), Warn)); // card keeps its size when the call changes
	const float W = PadL + 2.f * (RingR + 2.f) + Gap + MsgW + PadR;
	const FVector2D Pos(P.Size.X * 0.5f - W * 0.5f, P.Size.Y - 300.f - Hh * 0.5f + (1.f - In) * UI::Motion::Rise);
	P.ShadeCard(Pos, FVector2D(W, Hh), Al, 9);
	const FVector2D CC = Pos + FVector2D(PadL + RingR + 2.f, Hh * 0.5f);
	P.Ring(CC, RingR, A(UI::Cream, 0.2f * Al), 2.5f, 11);
	P.Arc(CC, RingR, 0.f, FMath::Max(E, 0.001f), A(UI::Sun, Al), 3.5f, 12);
	P.SunGlyph(CC, 7.f, E, Al, 0.f, 12);
	P.Text(Msg, FVector2D(CC.X + RingR + 2.f + Gap, CC.Y), Warn, A(E > 0.6f ? UI::Sun : UI::Cream, Al), 0.f, 0.5f, 0.f, 12);
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
	const float In = ShadeEase::OutCubic(ShadeEase::Window(T, 0.f, UI::Motion::In));
	const float Al = In * (1.f - ShadeEase::Window(T, Hold, Hold + UI::Motion::Out));
	if (Al <= 0.01f)
	{
		return;
	}
	// Narration on a shade card, centred low (above the controls strip and the letterbox); wraps at 1100, max 2 lines.
	const FSlateFontInfo Font = F(TEXT("BodyBold"), UI::Type::Narration, 0, P.S);
	const TArray<FString> Lines = P.Wrap(G->GetLine().ToString(), Font, UI::Type::NarrationWrap, 2);
	const float LineH = P.LineHeight(Font) * 0.95f;
	float MaxW = 0.f;
	for (const FString& Ln : Lines)
	{
		MaxW = FMath::Max(MaxW, P.Width(Ln, Font));
	}
	const float PadX = 32.f, PadY = 12.f;
	const FVector2D Sz(MaxW + 2.f * PadX, Lines.Num() * LineH + 2.f * PadY);
	const float Bottom = P.Size.Y - 128.f + (1.f - In) * UI::Motion::Rise;
	const FVector2D Pos(P.Size.X * 0.5f - Sz.X * 0.5f, Bottom - Sz.Y);
	P.ShadeCard(Pos, Sz, Al, 12);
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		P.Text(Lines[i], FVector2D(P.Size.X * 0.5f, Pos.Y + PadY + i * LineH), Font, A(UI::Cream, Al), 0.5f, 0.f, 0.f, 15);
	}
}

void SShadeOverlay::PaintToasts(const FPainter& P, double Now) const
{
	AShadeGameMode* G = GM();
	if (!G)
	{
		return;
	}
	const FSlateFontInfo Kick = F(TEXT("BodyHeavy"), UI::Type::Kicker, UI::Type::Tracking, P.S);
	const FSlateFontInfo Font = F(TEXT("Display"), UI::Type::Title, 0, P.S);
	const FString KickStr = TEXT("SHADE MADE");
	const float KickH = P.LineHeight(Kick);
	const float KickW = P.Width(KickStr, Kick);
	const float Cx = static_cast<float>(P.Size.X) * 0.5f;
	const float OutAt = UI::Motion::ToastIn + UI::Motion::ToastHold;
	float Y = UI::Margin;
	for (const FShadeToast& T : G->GetToasts())
	{
		const float Age = static_cast<float>(Now - T.StartTime);
		const float InT = ShadeEase::OutCubic(ShadeEase::Window(Age, 0.f, UI::Motion::ToastIn));
		const float Al = InT * (1.f - ShadeEase::Window(Age, OutAt, OutAt + UI::Motion::ToastOut)) * HudAlpha;
		if (Al <= 0.01f)
		{
			continue;
		}
		// Shade card: sun kicker over the count, dropping in from just above.
		const FString Str = T.Text.ToString();
		const FVector2D Sz = P.Measure(Str, Font);
		const float PadX = 40.f, PadY = 12.f;
		const float W = FMath::Max(static_cast<float>(Sz.X), KickW) + 2.f * PadX;
		const float Hh = PadY + KickH * 0.85f + static_cast<float>(Sz.Y) * 0.9f + PadY;
		const FVector2D Pos(Cx - W * 0.5f, Y - (1.f - InT) * UI::Motion::Rise);
		P.ShadeCard(Pos, FVector2D(W, Hh), Al, 9);
		P.Text(KickStr, FVector2D(Cx, Pos.Y + PadY), Kick, A(UI::Sun, Al), 0.5f, 0.f, 0.f, 12);
		P.Text(Str, FVector2D(Cx, Pos.Y + PadY + KickH * 0.85f), Font, A(UI::Cream, Al), 0.5f, 0.f, 0.f, 12);
		Y += Hh + 12.f;
	}
}

void SShadeOverlay::PaintControls(const FPainter& P, float Alpha, const FVector2D& Origin) const
{
	if (Alpha <= 0.01f)
	{
		return;
	}
	const FSlateFontInfo KeyF = F(TEXT("BodyHeavy"), UI::Type::Key, 40, P.S);
	const FSlateFontInfo Lab = F(TEXT("BodyHeavy"), UI::Type::Kicker, UI::Type::Tracking, P.S);
	const bool bPad = UsingPad();
	struct FRow { const TCHAR* K; const TCHAR* Pad; const TCHAR* Label; };
	const FRow Rows[] = { { TEXT("WASD"), TEXT("LS"), TEXT("MOVE") }, { TEXT("MOUSE"), TEXT("RS"), TEXT("LOOK") }, { TEXT("SPACE"), TEXT("A"), TEXT("JUMP") },
		{ TEXT("SHIFT"), TEXT("L3"), TEXT("TROT") }, { TEXT("E"), TEXT("X"), TEXT("USE") } };
	// One shade strip sized to its contents: keycap + caps label per action.
	const float KeyH = 30.f, PadX = 16.f, Gap = 10.f, Between = 26.f;
	float W = 2.f * PadX - Between;
	for (const FRow& R : Rows)
	{
		W += P.KeyWidth(KeyLabel(R.K, R.Pad), KeyF, KeyH, bPad) + Gap + P.Width(R.Label, Lab) + Between;
	}
	P.ShadeCard(Origin, FVector2D(W, UI::StripH), Alpha, 9);
	const float KY = static_cast<float>(Origin.Y) + (UI::StripH - KeyH) * 0.5f;
	float X = static_cast<float>(Origin.X) + PadX;
	for (const FRow& R : Rows)
	{
		const float KW = P.Key(KeyLabel(R.K, R.Pad), FVector2D(X, KY), KeyF, Alpha, KeyH, bPad, 11);
		const FVector2D Sz = P.Text(R.Label, FVector2D(X + KW + Gap, Origin.Y + UI::StripH * 0.5f), Lab, A(UI::Cream, 0.9f * Alpha), 0.f, 0.5f, 0.f, 13);
		X += KW + Gap + static_cast<float>(Sz.X) + Between;
	}
}

void SShadeOverlay::PaintTitle(const FPainter& P, double Now) const
{
	AShadeHUD* H = Owner.Get();
	const float T = static_cast<float>(Now - H->GetMenuOpenTime());
	const float S = P.S;
	auto Stage = [T](float A0, float B0) { return ShadeEase::OutCubic(ShadeEase::Window(T, A0, B0)); };
	const float Rise = UI::Motion::Rise;
	// The type sits in shade, like its hero: a soft wash darkens the left of the frame and the very bottom.
	const float Bg = Stage(0.f, 1.8f);
	P.Gradient(FVector2D(0, 0), FVector2D(P.Size.X * 0.6f, P.Size.Y), A(UI::Night, 0.7f * Bg), A(UI::Night, 0.f), Orient_Vertical, 1);
	P.Gradient(FVector2D(0, P.Size.Y - 260.f), FVector2D(P.Size.X, 260.f), A(UI::Night, 0.f), A(UI::Night, 0.55f * Bg), Orient_Horizontal, 1);

	const float X0 = 150.f, Top = 230.f;
	const FSlateFontInfo Kicker = F(TEXT("BodyHeavy"), UI::Type::Kicker, UI::Type::Tracking, S);
	const FSlateFontInfo Word = F(TEXT("Display"), UI::Type::Logo, 0, S);
	const FSlateFontInfo Amp = F(TEXT("DisplayItalic"), UI::Type::Logo, 0, S);
	const FSlateFontInfo TagF = F(TEXT("DisplayItalic"), UI::Type::Title, 0, S);
	// Fraunces: caps start ~23% down the line box and the baseline sits ~79% down.
	const float LogoH = P.LineHeight(Word);
	const float KickH = P.LineHeight(Kicker);
	const float Line2 = Top + LogoH * 0.75f;
	const float RuleY = Line2 + LogoH * 0.9f;

	const float K = Stage(0.4f, 1.4f);
	P.Text(TEXT("A STORY FROM VELLARA"), FVector2D(X0 + 8.f, Top + LogoH * 0.225f - KickH - 10.f + (1.f - K) * Rise), Kicker, A(UI::Sun, K), 0.f, 0.f, 0.5f, 6);
	const float W1 = Stage(0.7f, 2.0f);
	P.Display(TEXT("Shade"), FVector2D(X0, Top + (1.f - W1) * Rise), Word, A(UI::Cream, W1), 0.f, 0.f, 0.7f);
	const float W2 = Stage(1.0f, 2.3f);
	const FVector2D L2(X0 + 96.f, Line2 + (1.f - W2) * Rise);
	const FVector2D AmpSz = P.Display(TEXT("&"), L2, Amp, A(UI::Sun, W2), 0.f, 0.f, 0.6f);
	P.Display(TEXT("Seek"), L2 + FVector2D(AmpSz.X + 18.f, 0.f), Word, A(UI::Cream, W2), 0.f, 0.f, 0.7f);
	const float R = Stage(1.7f, 2.8f);
	P.Rule(FVector2D(X0 + 8.f, RuleY), 380.f * R, A(UI::Sun, 0.85f * R), 1.6f, 6);
	P.Circle(FVector2D(X0 + 8.f + 380.f * R + 10.f, RuleY + 0.8f), 3.f, A(UI::Sun, R), 6, 12);
	P.Display(TEXT("a little shadow's long way home"), FVector2D(X0 + 8.f, RuleY + 14.f), TagF, A(UI::Cream, 0.92f * Stage(1.9f, 3.0f)), 0.f, 0.f, 0.45f);

	PaintMenuItems(P, Now, FVector2D(X0 + 8.f, RuleY + 110.f), 1.f, false, 2.3f);
	const float FootA = Stage(2.7f, 3.6f);
	const FVector2D Strip(X0 + 8.f, P.Size.Y - UI::Margin - UI::StripH);
	PaintControls(P, 0.9f * FootA, Strip);
	P.Text(TEXT("VELLARA  ·  LATE AFTERNOON"), FVector2D(P.Size.X - 90.f, Strip.Y + UI::StripH * 0.5f), Kicker, A(UI::Cream, 0.75f * FootA), 1.f, 0.5f, 0.6f, 6);
}

void SShadeOverlay::PaintMenuItems(const FPainter& P, double Now, const FVector2D& Origin, float Alpha, bool bCentered, float Start) const
{
	AShadeHUD* H = Owner.Get();
	if (Alpha <= 0.01f)
	{
		return;
	}
	const float T = static_cast<float>(Now - H->GetMenuOpenTime());
	const FSlateFontInfo Item = F(TEXT("Display"), UI::Type::Menu, 0, P.S);
	const int32 N = H->GetMenuItemCount();
	const float Pitch = 58.f;
	for (int32 i = 0; i < N; ++i)
	{
		// Rows enter one after another (60 ms apart), each rising 8 px.
		const float RowStart = Start + i * UI::Motion::Stagger;
		const float In = ShadeEase::OutCubic(ShadeEase::Window(T, RowStart, RowStart + UI::Motion::In));
		const float RowA = Alpha * In;
		const FString Str = H->GetMenuItemLabel(i).ToString();
		const bool bSel = i == H->Selected;
		const FVector2D Sz = P.Measure(Str, Item);
		const float Y = Origin.Y + i * Pitch;
		const float X = bCentered ? Origin.X - Sz.X * 0.5f : Origin.X;
		const float DrawY = Y + (1.f - In) * UI::Motion::Rise;
		if (bSel)
		{
			// Sun underline under the selected row (Fraunces' baseline sits ~79% down its line box).
			P.RBox(FVector2D(X, DrawY + Sz.Y * 0.88f), FVector2D(Sz.X, 3.f), A(UI::Sun, RowA), 1.5f, 19);
		}
		P.Text(Str, FVector2D(X, DrawY), Item, A(UI::Cream, RowA * (bSel ? 1.f : 0.6f)), 0.f, 0.f, 0.35f, 20);
		ItemRects.Add(FBox2D(FVector2D(X - 44.f, Y), FVector2D(X + Sz.X + 44.f, Y + Sz.Y)));
	}
}

void SShadeOverlay::PaintPause(const FPainter& P, double Now) const
{
	AShadeHUD* H = Owner.Get();
	const float T = static_cast<float>(Now - H->GetMenuOpenTime());
	const float Al = ShadeEase::OutCubic(ShadeEase::Window(T, 0.f, UI::Motion::In));
	// A dim over the (blurred) world, then one shade panel: heading and menu on the left, controls on the right.
	P.Box(FVector2D(0, 0), P.Size, A(UI::Night, 0.45f * Al), &GSolid, 16);

	const FSlateFontInfo Kicker = F(TEXT("BodyHeavy"), UI::Type::Kicker, UI::Type::Tracking, P.S);
	const FSlateFontInfo Head = F(TEXT("Display"), UI::Type::Heading, 0, P.S);
	const FSlateFontInfo KeyF = F(TEXT("BodyHeavy"), UI::Type::Key, 40, P.S);
	const FSlateFontInfo Lab = F(TEXT("Body"), UI::Type::Body, 0, P.S);
	struct FRow { const TCHAR* K; const TCHAR* Pad; const TCHAR* Label; };
	const FRow Rows[] = { { TEXT("WASD"), TEXT("LS"), TEXT("Pad through the shade") }, { TEXT("MOUSE"), TEXT("RS"), TEXT("Look around") },
		{ TEXT("SPACE"), TEXT("A"), TEXT("Spring up") }, { TEXT("SHIFT"), TEXT("L3"), TEXT("Trot") },
		{ TEXT("E"), TEXT("X"), TEXT("Push, tug, nudge, wake") }, { TEXT("ESC"), TEXT("START"), TEXT("Siesta") } };
	const int32 NumRows = static_cast<int32>(UE_ARRAY_COUNT(Rows));
	const bool bPad = UsingPad();

	const float PadX = 64.f, PadY = 48.f, ColGap = 88.f, LeftW = 400.f, RightW = 390.f, RowH = 50.f, KeyH = 32.f, Pitch = 58.f;
	const float KickH = P.LineHeight(Kicker);
	const float HeadH = P.LineHeight(Head);
	// Left column, relative to the content origin: kicker, heading (caps ~23% down its line box), sun rule, menu.
	const float HeadY = KickH + 14.f - HeadH * 0.225f;
	const float RuleY = HeadY + HeadH * 0.793f + 20.f;
	const float MenuY = RuleY + 30.f;
	const float LeftH = MenuY + H->GetMenuItemCount() * Pitch;
	const float RightH = KickH + 20.f + NumRows * RowH;
	const float W = 2.f * PadX + LeftW + ColGap + RightW;
	const float Hh = 2.f * PadY + FMath::Max(LeftH, RightH) - 10.f;
	const FVector2D Pos(P.Size.X * 0.5f - W * 0.5f, P.Size.Y * 0.5f - Hh * 0.5f + (1.f - Al) * UI::Motion::Rise);
	P.ShadeCard(Pos, FVector2D(W, Hh), Al, 17);

	const FVector2D L0 = Pos + FVector2D(PadX, PadY);
	P.Text(TEXT("PAUSED  ·  A QUIET MOMENT"), L0, Kicker, A(UI::Sun, Al), 0.f, 0.f, 0.f, 20);
	P.Text(TEXT("Siesta"), L0 + FVector2D(0.f, HeadY), Head, A(UI::Cream, Al), 0.f, 0.f, 0.f, 20);
	P.Rule(L0 + FVector2D(0.f, RuleY), 300.f, A(UI::Sun, 0.8f * Al), 1.6f, 20);
	PaintMenuItems(P, Now, L0 + FVector2D(0.f, MenuY), Al, false, UI::Motion::Stagger);

	// Divider, then the controls reference.
	const float DX = static_cast<float>(L0.X) + LeftW + ColGap * 0.5f;
	P.Box(FVector2D(DX, Pos.Y + PadY), FVector2D(1.f, Hh - 2.f * PadY), A(UI::Cream, 0.14f * Al), &GSolid, 19);
	const float RX = static_cast<float>(L0.X) + LeftW + ColGap;
	P.Text(TEXT("PAWS & PADS"), FVector2D(RX, L0.Y), Kicker, A(UI::Sun, Al), 0.f, 0.f, 0.f, 20);
	float Y = static_cast<float>(L0.Y) + KickH + 20.f;
	float KeyCol = 110.f;  // label column: clear of the widest key (glyph WASD included)
	for (const FRow& Row : Rows)
	{
		KeyCol = FMath::Max(KeyCol, P.KeyWidth(KeyLabel(Row.K, Row.Pad), KeyF, KeyH, bPad) + 22.f);
	}
	for (const FRow& Row : Rows)
	{
		P.Key(KeyLabel(Row.K, Row.Pad), FVector2D(RX, Y), KeyF, Al, KeyH, bPad, 19);
		P.Text(Row.Label, FVector2D(RX + KeyCol, Y + (KeyH - 3.f) * 0.5f), Lab, A(UI::Cream, 0.92f * Al), 0.f, 0.5f, 0.f, 20);
		Y += RowH;
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
	// A dim over the reunion, then one centred shade panel.
	P.Box(FVector2D(0, 0), P.Size, A(UI::Night, 0.35f * In), &GSolid, 16);
	const FSlateFontInfo Kicker = F(TEXT("BodyHeavy"), UI::Type::Kicker, UI::Type::Tracking, S);
	const FSlateFontInfo Head = F(TEXT("Display"), UI::Type::Heading, 0, S);
	const FSlateFontInfo Sub = F(TEXT("DisplayItalic"), UI::Type::Title, 0, S);
	const FSlateFontInfo StatN = F(TEXT("Display"), UI::Type::Menu, 0, S);
	const FString SubStr = TEXT("Biscotto has his shadow back, just in time for supper.");

	const float PadX = 72.f, PadY = 48.f, Pitch = 58.f;
	const float KickH = P.LineHeight(Kicker);
	const float HeadH = P.LineHeight(Head);
	const float StatH = P.LineHeight(StatN);
	// Rows, relative to the panel's top.
	const float HeadY = PadY + KickH + 14.f - HeadH * 0.225f;
	const float RuleY = HeadY + HeadH * 0.793f + 20.f;
	const float SubY = RuleY + 16.f;
	const float StatY = SubY + P.LineHeight(Sub) + 30.f;
	const float LabY = StatY + StatH * 0.95f;
	const float MenuY = LabY + KickH + 36.f;
	const float W = FMath::Max(860.f, P.Width(SubStr, Sub) + 2.f * PadX);
	const float Hh = MenuY + H->GetMenuItemCount() * Pitch + PadY - 12.f;
	const float Cx = static_cast<float>(P.Size.X) * 0.5f;
	const FVector2D Pos(Cx - W * 0.5f, P.Size.Y * 0.5f - Hh * 0.5f + (1.f - In) * UI::Motion::Rise);
	P.ShadeCard(Pos, FVector2D(W, Hh), In, 17);

	P.Text(TEXT("HOME BEFORE SUPPER"), FVector2D(Cx, Pos.Y + PadY), Kicker, A(UI::Sun, In), 0.5f, 0.f, 0.f, 20);
	P.Text(TEXT("Reunited"), FVector2D(Cx, Pos.Y + HeadY), Head, A(UI::Cream, In), 0.5f, 0.f, 0.f, 20);
	const float R = Stage(1.0f, 2.0f);
	P.Rule(FVector2D(Cx - 200.f * R, Pos.Y + RuleY), 400.f * R, A(UI::Sun, 0.8f * R), 1.6f, 20);
	P.Text(SubStr, FVector2D(Cx, Pos.Y + SubY), Sub, A(UI::Cream, 0.95f * Stage(1.2f, 2.2f)), 0.5f, 0.f, 0.f, 20);

	const float SIn = Stage(1.8f, 2.6f);
	const FString Stats[3][2] = {
		{ G ? FormatTime(G->GetRunTime()) : TEXT("-"), TEXT("TIME") },
		{ FString::Printf(TEXT("%d / %d"), G ? G->GetPuzzlesSolved() : 0, AShadeGameMode::PuzzleCount), TEXT("SHADE MADE") },
		{ FString::FromInt(K ? K->GetEvaporations() : 0), TEXT("EVAPORATIONS") } };
	for (int32 i = 0; i < 3; ++i)
	{
		const float SX = Cx + (i - 1) * 240.f;
		P.Text(Stats[i][0], FVector2D(SX, Pos.Y + StatY), StatN, A(UI::Cream, SIn), 0.5f, 0.f, 0.f, 20);
		P.Text(Stats[i][1], FVector2D(SX, Pos.Y + LabY), Kicker, A(UI::Sun, SIn), 0.5f, 0.f, 0.f, 20);
		if (i > 0)
		{
			P.Box(FVector2D(SX - 120.f, Pos.Y + StatY + 8.f), FVector2D(1.f, StatH * 0.95f + KickH - 12.f), A(UI::Cream, 0.18f * SIn), &GSolid, 19);
		}
	}
	PaintMenuItems(P, Now, FVector2D(Cx, Pos.Y + MenuY), In, true, 2.4f);
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
	if (!H || !G || (PC() && PC()->IsAutoplaying()))
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
	// Two families only (both OFL): Fraunces for display, Nunito for everything else.
	struct FFace { const TCHAR* Name; const TCHAR* File; };
	const FFace Faces[] = {
		{ TEXT("Display"), TEXT("Fraunces-SemiBold.ttf") }, { TEXT("DisplayItalic"), TEXT("Fraunces-SemiBoldItalic.ttf") },
		{ TEXT("Body"), TEXT("Nunito-SemiBold.ttf") }, { TEXT("BodyBold"), TEXT("Nunito-Bold.ttf") },
		{ TEXT("BodyHeavy"), TEXT("Nunito-ExtraBold.ttf") },
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

	// Input glyphs (Kenney Input Prompts, CC0): white keys with cut-out letters, cropped to the key and tinted in Key().
	// Letter keys and the mouse only - SPACE / SHIFT / ESC keep the painted keycap, whose label stays legible at 30 px.
	const TCHAR* GlyphNames[] = { TEXT("W"), TEXT("A"), TEXT("S"), TEXT("D"), TEXT("E"), TEXT("MOUSE") };
	for (const TCHAR* G : GlyphNames)
	{
		const FString Path = FString::Printf(TEXT("/Game/Shade/UI/Glyphs/T_Key_%s.T_Key_%s"), G, G);
		if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path))
		{
			GlyphTextures.Add(Tex);
			FSlateBrush B;
			B.SetResourceObject(Tex);
			B.ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
			B.DrawAs = ESlateBrushDrawType::Image;
			GlyphBrushes.Add(FName(G), B);
		}
		else
		{
			UE_LOG(LogShade, Warning, TEXT("UI glyph missing: %s (keycap falls back to text)"), *Path);
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
				return Self->GetMenu() == EShadeMenu::Pause ? 10.f * ShadeEase::OutCubic(static_cast<float>(T / UI::Motion::In)) : 0.f;
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
		// Missing file: the engine's Roboto at the nearest weight.
		const TCHAR* Fallback = Face == TEXT("DisplayItalic") ? TEXT("BoldItalic") : (Face == TEXT("Body") ? TEXT("Regular") : TEXT("Bold"));
		Info = FCoreStyle::GetDefaultFontStyle(Fallback, FMath::RoundToInt(Px));
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
