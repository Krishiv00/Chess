#include "GUI/UI/Widgets.hpp"

#include <algorithm>
#include <cstdlib>


namespace {
    constexpr float Pi = 3.14159265f;

    int SegmentsFor(float radius) {
        return std::clamp(static_cast<int>(radius * 0.5f) + 3, 4, 12);
    }

    const std::vector<sf::Vector2f>& ArcTable(int segments) {
        static std::vector<sf::Vector2f> tables[13];

        std::vector<sf::Vector2f>& table = tables[segments];

        if (table.empty()) {
            for (int i = 0; i <= segments; ++i) {
                const float rad = 90.f * static_cast<float>(i) / static_cast<float>(segments) * Pi / 180.f;
                table.emplace_back(std::cos(rad), std::sin(rad));
            }
        }

        return table;
    }

    void AppendRoundedPath(
        std::vector<sf::Vector2f>& path, sf::Vector2f position, sf::Vector2f size, float radius, int segments
    ) {
        const sf::Vector2f centers[] = {
            {position.x + size.x - radius, position.y + radius},
            {position.x + size.x - radius, position.y + size.y - radius},
            {position.x + radius, position.y + size.y - radius},
            {position.x + radius, position.y + radius}
        };

        const std::vector<sf::Vector2f>& arc = ArcTable(segments);

        for (int corner = 0; corner < 4; ++corner) {
            for (const sf::Vector2f& unit : arc) {
                const sf::Vector2f direction = corner == 0 ? sf::Vector2f(unit.y, -unit.x)
                    : corner == 1 ? sf::Vector2f(unit.x, unit.y)
                    : corner == 2 ? sf::Vector2f(-unit.y, unit.x)
                    : sf::Vector2f(-unit.x, -unit.y);

                path.emplace_back(centers[corner].x + direction.x * radius, centers[corner].y + direction.y * radius);
            }
        }
    }

    std::vector<sf::Vector2f>& Path() {
        static thread_local std::vector<sf::Vector2f> path;
        path.clear();
        return path;
    }

    std::vector<sf::Vertex>& Vertices() {
        static thread_local std::vector<sf::Vertex> vertices;
        vertices.clear();
        return vertices;
    }
}

void Utils::RenderQuad(sf::RenderTarget& target, sf::Color color, sf::Vector2f position, sf::Vector2f size) {
    const sf::Vertex vertices[] = {
        sf::Vertex(position, color),
        sf::Vertex(sf::Vector2f(position.x + size.x, position.y), color),
        sf::Vertex(sf::Vector2f(position.x, position.y + size.y), color),
        sf::Vertex(position + size, color)
    };

    target.draw(vertices, 4, sf::PrimitiveType::TriangleStrip);
}

void Utils::RenderRoundedRect(
    sf::RenderTarget& target, sf::Color color, sf::Vector2f position, sf::Vector2f size, float radius
) {
    radius = std::clamp(radius, 0.f, std::min(size.x, size.y) * 0.5f);

    if (radius < 0.5f) return RenderQuad(target, color, position, size);

    std::vector<sf::Vector2f>& path = Path();
    AppendRoundedPath(path, position, size, radius, SegmentsFor(radius));

    std::vector<sf::Vertex>& vertices = Vertices();
    vertices.emplace_back(position + size * 0.5f, color);

    for (const sf::Vector2f& point : path) vertices.emplace_back(point, color);

    vertices.push_back(vertices[1]);

    target.draw(vertices.data(), vertices.size(), sf::PrimitiveType::TriangleFan);
}

void Utils::RenderRoundedQuad(
    sf::RenderTarget& target, sf::Color color, sf::Vector2f position, sf::Vector2f size, float roundness
) {
    RenderRoundedRect(target, color, position, size, std::min(size.x, size.y) * 0.5f * roundness);
}

void Utils::RenderRoundedFrame(
    sf::RenderTarget& target, sf::Color color, sf::Vector2f position, sf::Vector2f size, float radius, float thickness
) {
    radius = std::clamp(radius, 0.f, std::min(size.x, size.y) * 0.5f);
    thickness = std::min(thickness, std::min(size.x, size.y) * 0.5f);

    const int segments = SegmentsFor(radius);
    const sf::Vector2f innerPosition = position + sf::Vector2f(thickness, thickness);
    const sf::Vector2f innerSize = size - sf::Vector2f(thickness, thickness) * 2.f;

    std::vector<sf::Vector2f>& outer = Path();
    AppendRoundedPath(outer, position, size, radius, segments);

    static thread_local std::vector<sf::Vector2f> inner;
    inner.clear();
    AppendRoundedPath(inner, innerPosition, innerSize, std::max(0.f, radius - thickness), segments);

    std::vector<sf::Vertex>& vertices = Vertices();

    for (std::size_t i = 0; i < outer.size(); ++i) {
        vertices.emplace_back(outer[i], color);
        vertices.emplace_back(inner[i], color);
    }

    vertices.push_back(vertices[0]);
    vertices.push_back(vertices[1]);

    target.draw(vertices.data(), vertices.size(), sf::PrimitiveType::TriangleStrip);
}

void Utils::RenderFrame(
    sf::RenderTarget& target, sf::Color color, sf::Vector2f position, sf::Vector2f size, float thicknessFactor
) {
    const float thickness = std::min(size.x, size.y) * thicknessFactor;

    const float left = position.x;
    const float right = position.x + size.x;
    const float top = position.y;
    const float bottom = position.y + size.y;

    const float innerLeft = left + thickness;
    const float innerRight = right - thickness;
    const float innerTop = top + thickness;
    const float innerBottom = bottom - thickness;

    const sf::Vertex vertices[] = {
        sf::Vertex(sf::Vector2f(left, top), color),
        sf::Vertex(sf::Vector2f(innerLeft, innerTop), color),

        sf::Vertex(sf::Vector2f(right, top), color),
        sf::Vertex(sf::Vector2f(innerRight, innerTop), color),

        sf::Vertex(sf::Vector2f(right, bottom), color),
        sf::Vertex(sf::Vector2f(innerRight, innerBottom), color),

        sf::Vertex(sf::Vector2f(left, bottom), color),
        sf::Vertex(sf::Vector2f(innerLeft, innerBottom), color),

        sf::Vertex(sf::Vector2f(left, top), color),
        sf::Vertex(sf::Vector2f(innerLeft, innerTop), color)
    };

    target.draw(vertices, 10, sf::PrimitiveType::TriangleStrip);
}

#pragma region Particles

void ParticleSystem::RemoveAll() {
    m_Particles.clear();
}

void ParticleSystem::Spawn(std::size_t count, sf::FloatRect area) {
    m_Particles.clear();

    const float centerX = area.position.x + area.size.x * 0.5f;

    for (std::size_t i = 0; i < count; ++i) {
        Particle& p = m_Particles.emplace_back();

        const float spawnSpread = area.size.x * 0.4f;

        p.Position = sf::Vector2f(
            centerX + (std::rand() / static_cast<float>(RAND_MAX) - 0.5f) * spawnSpread * 2.f,
            area.position.y + area.size.y - (std::rand() % 10)
        );

        const float angle = -60.f - (std::rand() / static_cast<float>(RAND_MAX)) * 60.f;
        const float angleRad = angle * 3.14159265f / 180.f;
        const float speed = 220.f + (std::rand() / static_cast<float>(RAND_MAX)) * 500.f;

        p.Velocity = sf::Vector2f(
            std::cos(angleRad) * speed,
            std::sin(angleRad) * speed * 2.f
        );

        p.Rotation = static_cast<float>(std::rand() % 360);
        p.RotationSpeed = -300.f + (std::rand() % 600);

        const float sizeRand = std::rand() / static_cast<float>(RAND_MAX);
        p.Size = 4.f + sizeRand * sizeRand * 12.f;

        p.Life = 0.7f + 0.5f * (std::rand() / static_cast<float>(RAND_MAX));
        p.ColorIndex = std::rand() % 5;

        const int shapeRoll = std::rand() % 10;

        if (shapeRoll < 4) p.Shape = Particle::Shapes::Circle; // 40% circles
        else if (shapeRoll < 7) p.Shape = Particle::Shapes::Square; // 30% squares
        else p.Shape = Particle::Shapes::Diamond; // 30% diamonds
    }
}

void ParticleSystem::Update(float deltaTime) {
    constexpr float Gravity = 400.f;
    constexpr float AirResistance = 0.98f;

    for (auto it = m_Particles.begin(); it != m_Particles.end(); ) {
        Particle& p = *it;

        p.Velocity.y += Gravity * deltaTime;
        p.Velocity *= AirResistance;
        p.Position += p.Velocity * deltaTime;
        p.Rotation += p.RotationSpeed * deltaTime;
        p.Life -= deltaTime * 0.32f;

        if (p.Life <= 0.f) it = m_Particles.erase(it);
        else ++it;
    }
}

void ParticleSystem::Render(
    sf::RenderTarget& target, const std::array<sf::Color, 5>& colors, float opacityFactor
) const {
    if (m_Particles.empty()) return;

    constexpr int CircleSegments = 10;

    static const std::array<sf::Vector2f, CircleSegments + 1> unitCircle = []() {
        std::array<sf::Vector2f, CircleSegments + 1> points;

        for (int i = 0; i <= CircleSegments; ++i) {
            const float rad = 2.f * Pi * static_cast<float>(i) / static_cast<float>(CircleSegments);
            points[static_cast<std::size_t>(i)] = sf::Vector2f(std::cos(rad), std::sin(rad));
        }

        return points;
    }();

    std::vector<sf::Vertex>& vertices = Vertices();
    vertices.reserve(m_Particles.size() * CircleSegments * 3);

    const float fade = std::min(1.f, opacityFactor * 2.f);

    for (const Particle& p : m_Particles) {
        const float alpha = std::clamp(p.Life, 0.f, 1.f);

        sf::Color col = colors[p.ColorIndex];
        col.a = static_cast<std::uint8_t>(255.f * alpha * fade);

        const float half = p.Size * 0.5f;

        if (p.Shape == Particle::Shapes::Circle) {
            for (int i = 0; i < CircleSegments; ++i) {
                const sf::Vector2f a = unitCircle[static_cast<std::size_t>(i)];
                const sf::Vector2f b = unitCircle[static_cast<std::size_t>(i + 1)];

                vertices.emplace_back(p.Position, col);
                vertices.emplace_back(p.Position + a * half, col);
                vertices.emplace_back(p.Position + b * half, col);
            }
        } else {
            const float rad = (p.Shape == Particle::Shapes::Diamond ? p.Rotation + 45.f : p.Rotation) * Pi / 180.f;
            const float c = std::cos(rad) * half, s = std::sin(rad) * half;

            const sf::Vector2f tl = p.Position + sf::Vector2f(-c + s, -s - c);
            const sf::Vector2f tr = p.Position + sf::Vector2f(c + s, s - c);
            const sf::Vector2f br = p.Position + sf::Vector2f(c - s, s + c);
            const sf::Vector2f bl = p.Position + sf::Vector2f(-c - s, -s + c);

            vertices.emplace_back(tl, col);
            vertices.emplace_back(tr, col);
            vertices.emplace_back(br, col);

            vertices.emplace_back(tl, col);
            vertices.emplace_back(br, col);
            vertices.emplace_back(bl, col);
        }
    }

    target.draw(vertices.data(), vertices.size(), sf::PrimitiveType::Triangles);
}

#pragma region Evaluation Bar

void EvaluationBar::SetEval(float eval) {
    m_LatestEvaluation = eval;
}

void EvaluationBar::Update(float deltaTime) {
    m_CurrentEvaluation = Utils::ExponentiallyMoveTo(
        m_CurrentEvaluation, m_LatestEvaluation, 7.5f * deltaTime
    );
}

void EvaluationBar::Render(
    sf::RenderTarget& target, sf::Vector2f position, sf::Vector2f size,
    bool flipped, const std::array<sf::Color, 2>& colors
) const {
    const float padding_y = size.y * 0.006f;
    const float padding_x = size.x * 0.11f;

    const sf::Vector2u barSize(
        static_cast<unsigned int>(size.x - 2.f * padding_x),
        static_cast<unsigned int>(size.y - 2.f * padding_y)
    );

    if (barSize.x == 0 || barSize.y == 0) return;

    const float eval = flipped ? m_CurrentEvaluation : (1.f - m_CurrentEvaluation);
    const float midPos = static_cast<float>(barSize.y) * eval;

    const bool outdated = !m_Texture || m_TextureSize != barSize || m_DrawnMid != midPos ||
        m_DrawnFlipped != flipped || m_DrawnColors != colors;

    if (outdated) {
        if (!m_Texture || m_TextureSize != barSize) {
            m_Texture.emplace(barSize);
            m_TextureSize = barSize;
        }

        m_DrawnMid = midPos;
        m_DrawnFlipped = flipped;
        m_DrawnColors = colors;

        const float barWidth = static_cast<float>(barSize.x);
        const float barHeight = static_cast<float>(barSize.y);

        sf::RenderTexture& barRT = *m_Texture;
        barRT.clear(sf::Color::Transparent);

        Utils::RenderRoundedQuad(barRT, sf::Color::White, sf::Vector2f(), sf::Vector2f(barWidth, barHeight), 0.4f);

        sf::RectangleShape topFill(sf::Vector2f(barWidth, midPos));
        topFill.setFillColor(colors[!flipped]);
        barRT.draw(topFill, sf::BlendMultiply);

        sf::RectangleShape bottomFill(sf::Vector2f(barWidth, barHeight - midPos));
        bottomFill.setPosition(sf::Vector2f(0.f, midPos));
        bottomFill.setFillColor(colors[flipped]);
        barRT.draw(bottomFill, sf::BlendMultiply);

        barRT.display();
    }

    sf::Sprite bar(m_Texture->getTexture());
    bar.setPosition(sf::Vector2f(padding_x, padding_y) + position);
    target.draw(bar);
}
