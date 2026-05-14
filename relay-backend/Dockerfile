# ──────────────────────────────────────────────────────────────────────────────
# Stage 1 – Angular frontend
#   Build context root: webcam/
#   Source:  frontend/relay-frontend/
#   Output:  /workspace/relay-backend/src/main/resources/static/
#            (matches the outputPath set in angular.json)
# ──────────────────────────────────────────────────────────────────────────────
FROM node:22-alpine AS frontend

WORKDIR /workspace

# Layer cache: reinstall only when package files change
COPY frontend/relay-frontend/package.json \
     frontend/relay-frontend/package-lock.json \
     ./frontend/relay-frontend/

RUN npm ci --prefix ./frontend/relay-frontend

# Copy the rest of the Angular project (src, angular.json, tsconfig*, proxy.conf, public/)
COPY frontend/relay-frontend/ ./frontend/relay-frontend/

# angular.json production outputPath → ../../relay-backend/src/main/resources/static
# which resolves inside the container to:
#   /workspace/relay-backend/src/main/resources/static
RUN npm run build --prefix ./frontend/relay-frontend


# ──────────────────────────────────────────────────────────────────────────────
# Stage 2 – Spring Boot backend (Maven build, frontend skipped)
# ──────────────────────────────────────────────────────────────────────────────
FROM eclipse-temurin:21-jdk-alpine AS backend

WORKDIR /workspace

# Layer cache: download dependencies before copying source
COPY relay-backend/pom.xml ./relay-backend/
COPY relay-backend/.mvn  ./relay-backend/.mvn
COPY relay-backend/mvnw  ./relay-backend/mvnw
RUN chmod +x ./relay-backend/mvnw \
 && cd relay-backend \
 && ./mvnw dependency:go-offline -q -Dskip.frontend=true

# Copy backend source
COPY relay-backend/src ./relay-backend/src

# Overwrite the static directory with the Angular output produced in stage 1
COPY --from=frontend \
     /workspace/relay-backend/src/main/resources/static \
     ./relay-backend/src/main/resources/static

# Build jar; -Dskip.frontend=true suppresses the frontend-maven-plugin executions
RUN cd relay-backend \
 && ./mvnw package -DskipTests -Dskip.frontend=true -q


# ──────────────────────────────────────────────────────────────────────────────
# Stage 3 – Runtime image
#
# Uses the Debian (Jammy) variant instead of Alpine because Alpine's musl libc
# has no mDNS resolver.  libnss-mdns + nsswitch.conf gives the container the
# same .local resolution the host has, and --network host lets it reach the
# host's avahi daemon via multicast on the LAN interface.
# ──────────────────────────────────────────────────────────────────────────────
FROM eclipse-temurin:21-jre-jammy

RUN apt-get update \
 && apt-get install -y --no-install-recommends libnss-mdns \
 && rm -rf /var/lib/apt/lists/* \
 && sed -i \
      's/^hosts:.*/hosts: files mdns4_minimal [NOTFOUND=return] dns mdns4/' \
      /etc/nsswitch.conf

WORKDIR /app

COPY --from=backend /workspace/relay-backend/target/relay-backend-*.jar app.jar

EXPOSE 8080

ENTRYPOINT ["java", "-jar", "app.jar"]
