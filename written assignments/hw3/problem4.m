clc; clear; close all;

% Plot func is programmed by AI

rng(1);

Lx = 1;                  % region: [0, Lx]
Ly = 1;                  % region: [0, Ly]
Nx = 5;                  % number of square cells along x
Ny = 4;                  % number of square cells along y
M  = Nx * Ny;

Nsample = 200000;
maxIter = 100;
tol = 1e-10;

X = [Lx * rand(Nsample,1), Ly * rand(Nsample,1)];

C0 = square_grid_codebook(Nx, Ny, Lx, Ly);

[C_hist, D_hist, idx_hist] = lloyd2d_with_history(X, C0, [0 Lx 0 Ly], maxIter, tol);

nIter = length(C_hist);

fprintf('Total stored iterations = %d\n', nIter);
fprintf('Initial distortion      = %.10f\n', D_hist(1));
fprintf('Final distortion        = %.10f\n', D_hist(end));
fprintf('Relative decrease       = %.10f %%\n', 100*(D_hist(1)-D_hist(end))/D_hist(1));

%% -------------------- Distortion curve -------------------
figure;
plot(0:nIter-1, D_hist, 'o-', 'LineWidth', 1.3, 'MarkerSize', 6);
grid on;
xlabel('Iteration');
ylabel('Average distortion');
title('Distortion versus iteration');

%% -------------------- Selected iterations ----------------
sel = unique(round(linspace(1, nIter, min(nIter,6))));

figure;
for k = 1:length(sel)
    it = sel(k);
    subplot(2, ceil(length(sel)/2), k);
    plot_bounded_voronoi(C_hist{it}, [0 Lx 0 Ly]);
    hold on;
    plot(C_hist{it}(:,1), C_hist{it}(:,2), 'r.', 'MarkerSize', 16);
    title(sprintf('Iteration %d', it-1));
    axis equal; axis([0 Lx 0 Ly]); box on;
end

function C = square_grid_codebook(Nx, Ny, Lx, Ly)
    dx = Lx / Nx;
    dy = Ly / Ny;
    xc = dx/2 : dx : Lx-dx/2;
    yc = dy/2 : dy : Ly-dy/2;
    [XX, YY] = meshgrid(xc, yc);
    C = [XX(:), YY(:)];
end

function [C_hist, D_hist, idx_hist] = lloyd2d_with_history(X, Cinit, box, maxIter, tol)
    C = Cinit;
    C_hist = {};
    D_hist = [];
    idx_hist = {};

    prevD = inf;

    for it = 1:maxIter+1
        D2 = pdist2(X, C, 'squaredeuclidean');
        [mind2, idx] = min(D2, [], 2);
        Dcur = mean(mind2);

        C_hist{end+1} = C;
        D_hist(end+1,1) = Dcur;
        idx_hist{end+1} = idx;

        if it > 1
            if abs(prevD - Dcur) / Dcur < tol
                break;
            end
        end
        prevD = Dcur;

        Cold = C;
        M = size(C,1);

        for m = 1:M
            Xm = X(idx == m, :);
            if ~isempty(Xm)
                C(m,:) = mean(Xm, 1);
            else
                C(m,:) = Cold(m,:);
            end
        end

        if norm(C - Cold, 'fro') < 1e-14
            break;
        end
    end
end

function plot_bounded_voronoi(C, box)
    xmin = box(1); xmax = box(2);
    ymin = box(3); ymax = box(4);

    hold on;
    axis equal;

    % draw outer boundary
    rectangle('Position', [xmin, ymin, xmax-xmin, ymax-ymin], ...
              'EdgeColor', 'k', 'LineWidth', 1.2);

    M = size(C,1);

    % initial bounding polygon (counterclockwise)
    box_poly = [xmin ymin;
                xmax ymin;
                xmax ymax;
                xmin ymax];

    for i = 1:M
        poly = box_poly;

        pi = C(i,:);

        % Intersect the bounding box with all half-planes:
        % region of points closer to pi than to pj
        for j = 1:M
            if j == i
                continue;
            end

            pj = C(j,:);

            % Half-plane:
            % ||x-pi||^2 <= ||x-pj||^2
            % equivalent to:
            % 2 (pj-pi) x <= ||pj||^2 - ||pi||^2
            a = 2 * (pj(1) - pi(1));
            b = 2 * (pj(2) - pi(2));
            c = pj(1)^2 + pj(2)^2 - pi(1)^2 - pi(2)^2;

            poly = clip_polygon_halfplane(poly, a, b, c);

            if isempty(poly)
                break;
            end
        end

        if ~isempty(poly)
            patch(poly(:,1), poly(:,2), 'w', ...
                  'EdgeColor', [0 0.4470 0.7410], ...
                  'LineWidth', 1.0, ...
                  'FaceColor', 'none');
        end
    end
end

function poly_out = clip_polygon_halfplane(poly, a, b, c)
    % Clip polygon by half-plane a*x + b*y <= c
    if isempty(poly)
        poly_out = [];
        return;
    end

    n = size(poly,1);
    poly_out = [];

    for k = 1:n
        P = poly(k,:);
        Q = poly(mod(k,n)+1,:);

        fP = a*P(1) + b*P(2) - c;
        fQ = a*Q(1) + b*Q(2) - c;

        insideP = (fP <= 1e-12);
        insideQ = (fQ <= 1e-12);

        if insideP && insideQ
            poly_out = [poly_out; Q];
        elseif insideP && ~insideQ
            I = segment_halfplane_intersection(P, Q, a, b, c);
            poly_out = [poly_out; I];
        elseif ~insideP && insideQ
            I = segment_halfplane_intersection(P, Q, a, b, c);
            poly_out = [poly_out; I; Q];
        end
    end

    poly_out = remove_duplicate_vertices(poly_out);
end

function I = segment_halfplane_intersection(P, Q, a, b, c)
    % Solve for intersection of segment P->Q with line a*x+b*y=c
    d = Q - P;
    denom = a*d(1) + b*d(2);

    if abs(denom) < 1e-14
        I = P;  % nearly parallel, fallback
        return;
    end

    t = (c - a*P(1) - b*P(2)) / denom;
    t = max(0, min(1, t));
    I = P + t * d;
end

function poly = remove_duplicate_vertices(poly)
    if isempty(poly)
        return;
    end

    keep = true(size(poly,1),1);
    for k = 2:size(poly,1)
        if norm(poly(k,:) - poly(k-1,:)) < 1e-12
            keep(k) = false;
        end
    end
    poly = poly(keep,:);

    if size(poly,1) >= 2
        if norm(poly(1,:) - poly(end,:)) < 1e-12
            poly(end,:) = [];
        end
    end
end