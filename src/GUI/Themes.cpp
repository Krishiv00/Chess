#include "GUI/Themes.hpp"

static void RenderRing(
    sf::RenderTarget& target,
    sf::Color color,
    sf::Vector2f position,
    float squareSize
) {
    const float radius = squareSize * 0.5f;

    static sf::CircleShape ring = []() {
        sf::CircleShape shape;
        shape.setFillColor(sf::Color::Transparent);
        shape.setOutlineThickness(-7.f);
        return shape;
    }();

    static float builtRadius = -1.f;

    if (builtRadius != radius) {
        ring.setRadius(radius);
        ring.setOrigin(sf::Vector2f(radius, radius));
        builtRadius = radius;
    }

    ring.setOutlineColor(color);
    ring.setPosition(sf::Vector2f(position.x + squareSize * 0.5f, position.y + squareSize * 0.5f));

    target.draw(ring);
}

using namespace Themes;

#pragma region Chess.com

Chesscom::Chesscom() {
    m_Name = "Chess.com Default";
    m_Checkerboard = {
        sf::Color(235, 236, 208),
        sf::Color(115, 149, 82)
    };

    m_EvaluationBar = {
        sf::Color(255, 255, 255),
        sf::Color(64, 61, 57)
    };

    m_Particles = {
        sf::Color(235, 236, 208),
        sf::Color(115, 149, 82),
        sf::Color(180, 200, 140),
        sf::Color(255, 240, 180),
        sf::Color(80, 110, 55)
    };

    m_LastMoveHighlight = sf::Color(255, 255, 52, 128);
    m_LegalMoveHighlight = sf::Color(2, 1, 0, 36);
    m_MarkerHighlight = sf::Color(235, 97, 81, 204);
    m_Arrow = sf::Color(255, 170, 0, 163);
}

void Chesscom::RenderMarker(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const {
    const sf::Vertex vertices[] = {
        sf::Vertex(position, m_MarkerHighlight),
        sf::Vertex(sf::Vector2f(position.x + squareSize, position.y), m_MarkerHighlight),
        sf::Vertex(sf::Vector2f(position.x, position.y + squareSize), m_MarkerHighlight),
        sf::Vertex(sf::Vector2f(position.x + squareSize, position.y + squareSize), m_MarkerHighlight)
    };

    target.draw(vertices, 4, sf::PrimitiveType::TriangleStrip);
}

void Chesscom::RenderCaptureOverlay(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const {
    RenderRing(target, m_LegalMoveHighlight, position, squareSize);
}

sf::Color Chesscom::GetSelectedPiece() const {
    return m_LastMoveHighlight;
}

#pragma region Chesscom Variants

Themes::ChesscomGlass::ChesscomGlass() {
    m_Name = "Chess.com Glass";
    m_Checkerboard = {
        sf::Color(105, 114, 131),
        sf::Color(37, 43, 55)
    };

    m_EvaluationBar = {
        sf::Color(190, 200, 220),
        sf::Color(62, 72, 92)
    };

    m_Particles = {
        sf::Color(105, 114, 131),
        sf::Color(37, 43, 55),
        sf::Color(70, 85, 110),
        sf::Color(150, 175, 210),
        sf::Color(25, 30, 45)
    };

    m_LastMoveHighlight = sf::Color(40, 86, 114, 127);
}

Themes::ChesscomBrown::ChesscomBrown() {
    m_Name = "Chess.com Brown";
    m_Checkerboard = {
        sf::Color(237, 214, 176),
        sf::Color(184, 135, 98)
    };

    m_EvaluationBar = {
        sf::Color(237, 214, 176),
        sf::Color(80, 52, 30)
    };

    m_Particles = {
        sf::Color(237, 214, 176),
        sf::Color(184, 135, 98),
        sf::Color(210, 175, 130),
        sf::Color(255, 230, 160),
        sf::Color(130, 90, 55)
    };

    m_LastMoveHighlight = sf::Color(255, 255, 51, 128);
}

Themes::ChesscomSky::ChesscomSky() {
    m_Name = "Chess.com Sky";
    m_Checkerboard = {
        sf::Color(240, 241, 240),
        sf::Color(196, 216, 228)
    };

    m_EvaluationBar = {
        sf::Color(240, 248, 255),
        sf::Color(60, 100, 130)
    };

    m_Particles = {
        sf::Color(240, 241, 240),
        sf::Color(196, 216, 228),
        sf::Color(200, 225, 240),
        sf::Color(255, 255, 255),
        sf::Color(140, 180, 210)
    };

    m_LastMoveHighlight = sf::Color(104, 217, 248, 129);
}

Themes::Chesscom8Bit::Chesscom8Bit() {
    m_Name = "Chess.com 8-Bit";
    m_Checkerboard = {
        sf::Color(243, 243, 244),
        sf::Color(106, 155, 65)
    };

    m_EvaluationBar = {
        sf::Color(243, 243, 244),
        sf::Color(72, 108, 44)
    };

    m_Particles = {
        sf::Color(243, 243, 244),
        sf::Color(106, 155, 65),
        sf::Color(175, 200, 120),
        sf::Color(255, 255, 200),
        sf::Color(65, 110, 35)
    };

}

Themes::ChesscomPurple::ChesscomPurple() {
    m_Name = "Chess.com Purple";
    m_Checkerboard = {
        sf::Color(240, 241, 240),
        sf::Color(132, 118, 186)
    };

    m_EvaluationBar = {
        sf::Color(235, 230, 250),
        sf::Color(88, 72, 138)
    };

    m_Particles = {
        sf::Color(240, 241, 240),
        sf::Color(132, 118, 186),
        sf::Color(185, 175, 215),
        sf::Color(255, 245, 255),
        sf::Color(90, 75, 145)
    };

    m_LastMoveHighlight = sf::Color(126, 171, 202, 128);
}

Themes::ChesscomLight::ChesscomLight() {
    m_Name = "Chess.com Light";
    m_Checkerboard = {
        sf::Color(216, 217, 216),
        sf::Color(168, 169, 168)
    };

    m_EvaluationBar = {
        sf::Color(225, 225, 225),
        sf::Color(115, 115, 115)
    };

    m_Particles = {
        sf::Color(216, 217, 216),
        sf::Color(168, 169, 168),
        sf::Color(195, 196, 195),
        sf::Color(240, 240, 240),
        sf::Color(120, 121, 120)
    };

    m_LastMoveHighlight = sf::Color(131, 142, 149, 127);
}

Themes::ChesscomDark::ChesscomDark() {
    m_Name = "Chess.com Dark";
    m_Checkerboard = {
        sf::Color(144, 143, 141),
        sf::Color(110, 109, 107)
    };

    m_EvaluationBar = {
        sf::Color(195, 194, 192),
        sf::Color(72, 71, 70)
    };

    m_Particles = {
        sf::Color(144, 143, 141),
        sf::Color(110, 109, 107),
        sf::Color(128, 128, 126),
        sf::Color(180, 178, 175),
        sf::Color(75, 74, 73)
    };

    m_LastMoveHighlight = sf::Color(92, 145, 181, 128);
}

Themes::ChesscomDarkBlue::ChesscomDarkBlue() {
    m_Name = "Chess.com Dark Blue";
    m_Checkerboard = {
        sf::Color(234, 233, 210),
        sf::Color(75, 115, 153)
    };

    m_EvaluationBar = {
        sf::Color(234, 233, 210),
        sf::Color(85, 120, 158)
    };

    m_Particles = {
        sf::Color(234, 233, 210),
        sf::Color(75, 115, 153),
        sf::Color(155, 175, 195),
        sf::Color(220, 235, 255),
        sf::Color(45, 75, 110)
    };

    m_LastMoveHighlight = sf::Color(0, 165, 255, 127);
}

Themes::ChesscomBubblegum::ChesscomBubblegum() {
    m_Name = "Chess.com Bubblegum";
    m_Checkerboard = {
        sf::Color(254, 255, 254),
        sf::Color(251, 217, 225)
    };

    m_EvaluationBar = {
        sf::Color(255, 245, 248),
        sf::Color(140, 55, 78)
    };

    m_Particles = {
        sf::Color(254, 255, 254),
        sf::Color(251, 217, 225),
        sf::Color(252, 235, 240),
        sf::Color(255, 255, 255),
        sf::Color(225, 155, 175)
    };

    m_LastMoveHighlight = sf::Color(218, 71, 92, 112);
}

Themes::ChesscomCheckers::ChesscomCheckers() {
    m_Name = "Chess.com Checkers";
    m_Checkerboard = {
        sf::Color(199, 76, 81),
        sf::Color(48, 48, 48)
    };

    m_EvaluationBar = {
        sf::Color(248, 224, 136),
        sf::Color(88, 58, 58)
    };

    m_Particles = {
        sf::Color(199, 76, 81),
        sf::Color(48, 48, 48),
        sf::Color(140, 55, 58),
        sf::Color(248, 200, 120),
        sf::Color(30, 30, 30)
    };

    m_LastMoveHighlight = sf::Color(248, 224, 136, 129);
}

Themes::ChesscomOrange::ChesscomOrange() {
    m_Name = "Chess.com Orange";
    m_Checkerboard = {
        sf::Color(250, 228, 174),
        sf::Color(209, 136, 21)
    };

    m_EvaluationBar = {
        sf::Color(250, 228, 174),
        sf::Color(80, 44, 8)
    };

    m_Particles = {
        sf::Color(250, 228, 174),
        sf::Color(209, 136, 21),
        sf::Color(230, 185, 95),
        sf::Color(255, 245, 190),
        sf::Color(155, 95, 10)
    };

}

Themes::ChesscomRed::ChesscomRed() {
    m_Name = "Chess.com Red";
    m_Checkerboard = {
        sf::Color(245, 219, 195),
        sf::Color(187, 87, 70)
    };

    m_EvaluationBar = {
        sf::Color(245, 219, 195),
        sf::Color(80, 28, 20)
    };

    m_Particles = {
        sf::Color(245, 219, 195),
        sf::Color(187, 87, 70),
        sf::Color(220, 155, 130),
        sf::Color(255, 235, 210),
        sf::Color(140, 55, 40)
    };

    m_LastMoveHighlight = sf::Color(255, 255, 95, 128);
}

Themes::ChesscomTan::ChesscomTan() {
    m_Name = "Chess.com Tan";
    m_Checkerboard = {
        sf::Color(237, 203, 165),
        sf::Color(216, 164, 109)
    };

    m_EvaluationBar = {
        sf::Color(237, 203, 165),
        sf::Color(72, 44, 18)
    };

    m_Particles = {
        sf::Color(237, 203, 165),
        sf::Color(216, 164, 109),
        sf::Color(228, 185, 140),
        sf::Color(255, 230, 185),
        sf::Color(165, 115, 65)
    };

}

Themes::ChesscomBlue::ChesscomBlue() {
    m_Name = "Chess.com Blue";
    m_Checkerboard = {
        sf::Color(242, 246, 250),
        sf::Color(85, 150, 242)
    };

    m_EvaluationBar = {
        sf::Color(242, 246, 250),
        sf::Color(55, 95, 175)
    };

    m_Particles = {
        sf::Color(242, 246, 250),
        sf::Color(85, 150, 242),
        sf::Color(160, 200, 248),
        sf::Color(220, 240, 255),
        sf::Color(40, 90, 175)
    };

    m_LastMoveHighlight = sf::Color(0, 255, 242, 127);
}

Themes::ChesscomPink::ChesscomPink() {
    m_Name = "Chess.com Pink";
    m_Checkerboard = {
        sf::Color(245, 240, 241),
        sf::Color(236, 148, 164)
    };

    m_EvaluationBar = {
        sf::Color(248, 240, 243),
        sf::Color(110, 38, 58)
    };

    m_Particles = {
        sf::Color(245, 240, 241),
        sf::Color(236, 148, 164),
        sf::Color(242, 195, 205),
        sf::Color(255, 240, 245),
        sf::Color(190, 90, 115)
    };

    m_LastMoveHighlight = sf::Color(255, 115, 58, 128);
}

Themes::ChesscomTheMusical::ChesscomTheMusical() {
    m_Name = "Chess.com The Musical";
    m_Checkerboard = {
        sf::Color(215, 212, 212),
        sf::Color(128, 123, 118)
    };

    m_EvaluationBar = {
        sf::Color(220, 215, 225),
        sf::Color(108, 88, 115)
    };

    m_Particles = {
        sf::Color(215, 212, 212),
        sf::Color(128, 123, 118),
        sf::Color(175, 170, 165),
        sf::Color(240, 220, 240),
        sf::Color(80, 75, 72)
    };

    m_LastMoveHighlight = sf::Color(255, 0, 255, 128);
}

#pragma region Lichess

Lichess::Lichess() {
    m_Name = "Lichess Default";
    m_Checkerboard = {
        sf::Color(240, 217, 181),
        sf::Color(181, 136, 99)
    };

    m_EvaluationBar = {
        sf::Color(255, 255, 255),
        sf::Color(64, 61, 57)
    };

    m_Particles = {
        sf::Color(240, 217, 181),
        sf::Color(181, 136, 99),
        sf::Color(200, 185, 150),
        sf::Color(255, 230, 180),
        sf::Color(120, 100, 75)
    };

    m_LastMoveHighlight = sf::Color(155, 200, 0, 105);
    m_LegalMoveHighlight = sf::Color(70, 115, 65, 166);
    m_MarkerHighlight = sf::Color(19, 119, 24, 151);
    m_Arrow = sf::Color(19, 119, 24, 151);
}

void Lichess::RenderMarker(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const {
    RenderRing(target, m_MarkerHighlight, position, squareSize);
}

void Lichess::RenderCaptureOverlay(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const {
    const sf::Vector2f triangleSize = sf::Vector2f(squareSize, squareSize) * 0.167f;

    const sf::Vertex topLeft[] = {
        sf::Vertex(position, m_LegalMoveHighlight),
        sf::Vertex(sf::Vector2f(position.x + triangleSize.x, position.y), m_LegalMoveHighlight),
        sf::Vertex(sf::Vector2f(position.x, position.y + triangleSize.y), m_LegalMoveHighlight)
    };

    const sf::Vertex topRight[] = {
        sf::Vertex(sf::Vector2f(position.x + squareSize, position.y), m_LegalMoveHighlight),
        sf::Vertex(sf::Vector2f(position.x + squareSize - triangleSize.x, position.y), m_LegalMoveHighlight),
        sf::Vertex(sf::Vector2f(position.x + squareSize, position.y + triangleSize.y), m_LegalMoveHighlight)
    };

    const sf::Vertex bottomLeft[] = {
        sf::Vertex(sf::Vector2f(position.x, position.y + squareSize), m_LegalMoveHighlight),
        sf::Vertex(sf::Vector2f(position.x + triangleSize.x, position.y + squareSize), m_LegalMoveHighlight),
        sf::Vertex(sf::Vector2f(position.x, position.y + squareSize - triangleSize.y), m_LegalMoveHighlight)
    };

    const sf::Vertex bottomRight[] = {
        sf::Vertex(sf::Vector2f(position.x + squareSize, position.y + squareSize), m_LegalMoveHighlight),
        sf::Vertex(sf::Vector2f(position.x + squareSize - triangleSize.x, position.y + squareSize), m_LegalMoveHighlight),
        sf::Vertex(sf::Vector2f(position.x + squareSize, position.y + squareSize - triangleSize.y), m_LegalMoveHighlight)
    };

    target.draw(topLeft, 3, sf::PrimitiveType::Triangles);
    target.draw(topRight, 3, sf::PrimitiveType::Triangles);
    target.draw(bottomLeft, 3, sf::PrimitiveType::Triangles);
    target.draw(bottomRight, 3, sf::PrimitiveType::Triangles);
}

sf::Color Lichess::GetSelectedPiece() const {
    return m_LegalMoveHighlight;
}

#pragma region Lichess Variants

Themes::LichessBlue::LichessBlue() {
    m_Name = "Lichess Blue";
    m_Checkerboard = {
        sf::Color(222, 227, 230),
        sf::Color(140, 162, 173)
    };

    m_EvaluationBar = {
        sf::Color(222, 227, 230),
        sf::Color(105, 130, 148)
    };

    m_Particles = {
        sf::Color(222, 227, 230),
        sf::Color(140, 162, 173),
        sf::Color(180, 195, 205),
        sf::Color(240, 245, 250),
        sf::Color(90, 115, 130)
    };

    m_LastMoveHighlight = sf::Color(156, 200, 0, 104);
}

Themes::LichessGreen::LichessGreen() {
    m_Name = "Lichess Green";
    m_Checkerboard = {
        sf::Color(255, 255, 221),
        sf::Color(134, 166, 102)
    };

    m_EvaluationBar = {
        sf::Color(255, 255, 221),
        sf::Color(72, 108, 52)
    };

    m_Particles = {
        sf::Color(255, 255, 221),
        sf::Color(134, 166, 102),
        sf::Color(195, 215, 160),
        sf::Color(255, 255, 230),
        sf::Color(85, 120, 60)
    };

    m_LastMoveHighlight = sf::Color(1, 156, 199, 105);
}

Themes::LichessDark::LichessDark() {
    m_Name = "Lichess Dark";
    m_Checkerboard = {
        sf::Color(167, 167, 167),
        sf::Color(135, 135, 135)
    };

    m_EvaluationBar = {
        sf::Color(200, 200, 200),
        sf::Color(112, 112, 112)
    };

    m_Particles = {
        sf::Color(167, 167, 167),
        sf::Color(135, 135, 135),
        sf::Color(152, 152, 152),
        sf::Color(200, 200, 200),
        sf::Color(90, 90, 90)
    };

    m_LastMoveHighlight = sf::Color(155, 208, 0, 88);
}

Themes::LichessPurple::LichessPurple() {
    m_Name = "Lichess Purple";
    m_Checkerboard = {
        sf::Color(159, 144, 176),
        sf::Color(125, 74, 141)
    };

    m_EvaluationBar = {
        sf::Color(210, 200, 228),
        sf::Color(118, 80, 142)
    };

    m_Particles = {
        sf::Color(159, 144, 176),
        sf::Color(125, 74, 141),
        sf::Color(144, 110, 160),
        sf::Color(210, 195, 225),
        sf::Color(80, 45, 98)
    };

    m_LastMoveHighlight = sf::Color(157, 200, 0, 104);
}

const std::vector<Themes::Info>& Themes::List() {
    static const std::vector<Info> list = []() {
        std::vector<Info> result;

        for (const Factory factory : Factories) {
            const std::unique_ptr<Theme> theme = factory();
            result.push_back({std::string(theme->GetName()), theme->GetCheckerboard()});
        }

        return result;
    }();

    return list;
}

std::unique_ptr<Themes::Theme> Themes::Create(std::size_t index) {
    return Factories[index < Count ? index : 0]();
}

std::size_t Themes::IndexOf(std::string_view name) {
    const std::vector<Info>& list = List();

    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].Name == name) return i;
    }

    return 0;
}
