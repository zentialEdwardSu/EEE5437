clc; clear; close all;

sigma2 = 100;
sigma  = sqrt(sigma2);
%% Max
Mmax = 100;
Dvals = zeros(Mmax,1);
Rvals = zeros(Mmax,1);

for M = 1:Mmax
    [xhat, t, Dvals(M), Rvals(M)] = max_quantizer_gaussian(M, sigma);
    fprintf('M = %3d, D = %12.6f, R = %10.6f bits\n', M, Dvals(M), Rvals(M));
end

%% Theoretical Gaussian rate-distortion
Dth = logspace(log10(min(Dvals)*0.8), log10(sigma2), 500);
Rth = 0.5 * log2(sigma2 ./ Dth);
Rth(Dth > sigma2) = 0;

%% Plot R_M(D_M) and compare with theory
figure;
plot(Dvals, Rvals, 'bo', 'MarkerSize', 4, 'LineWidth', 1.0); hold on;
plot(Dth, Rth, 'r-', 'LineWidth', 1.5);
set(gca, 'XScale', 'log');
grid on;
xlabel('D');
ylabel('R(D)');
title('R_M(D_M) for Gaussian \sigma^2 = 100');
legend('Max quantizer', 'Theoretical R(D)', 'Location', 'northeast');

function [xhat, t, D, R] = max_quantizer_gaussian(M, sigma)
% Max algorithm for Gaussian source N(0, sigma^2)

maxIter = 200;
tol = 1e-10;

% Special case: M = 1
if M == 1
    xhat = 0;
    t = [-inf, inf];
    D = sigma^2;
    R = 0;
    return;
end

% Symmetric initialization for decision thresholds
L = 6 * sigma;                        % truncate range for initialization only
t_inner = linspace(-L, L, M+1);       % M cells
t = t_inner;
t(1) = -inf;
t(end) = inf;

D_old = inf;

for iter = 1:maxIter
    % centroid update
    xhat = zeros(M,1);
    p = zeros(M,1);

    for i = 1:M
        a = t(i);
        b = t(i+1);

        p(i) = gauss_prob(a, b, sigma);

        if p(i) < 1e-15
            % fallback to midpoint if a cell is almost empty
            if isinf(a) && isinf(b)
                xhat(i) = 0;
            elseif isinf(a)
                xhat(i) = b - sigma;
            elseif isinf(b)
                xhat(i) = a + sigma;
            else
                xhat(i) = 0.5 * (a + b);
            end
        else
            m1 = gauss_first_moment(a, b, sigma);
            xhat(i) = m1 / p(i);
        end
    end

    % Step 3: nearest-neighbor update
    for i = 2:M
        t(i) = 0.5 * (xhat(i-1) + xhat(i));
    end
    t(1) = -inf;
    t(end) = inf;

    % Step 4: distortion
    D = 0;
    for i = 1:M
        a = t(i);
        b = t(i+1);
        p_i  = gauss_prob(a, b, sigma);
        m1_i = gauss_first_moment(a, b, sigma);
        m2_i = gauss_second_moment(a, b, sigma);

        D = D + (m2_i - 2*xhat(i)*m1_i + xhat(i)^2 * p_i);
    end

    % Step 5: stopping criterion
    if abs(D_old - D) / D < tol
        break;
    end
    D_old = D;
end

% Entropy of reconstruction levels
p = zeros(M,1);
for i = 1:M
    p(i) = gauss_prob(t(i), t(i+1), sigma);
end
p = p(p > 0);
R = -sum(p .* log2(p));
end


function p = gauss_prob(a, b, sigma)
% P(a <= X < b), X ~ N(0, sigma^2)
p = normcdf(b / sigma) - normcdf(a / sigma);
end

function m1 = gauss_first_moment(a, b, sigma)
% ∫_a^b x f_X(x) dx for X ~ N(0, sigma^2)
fa = gauss_pdf(a, sigma);
fb = gauss_pdf(b, sigma);
m1 = sigma^2 * (fa - fb);
end

function m2 = gauss_second_moment(a, b, sigma)
% ∫_a^b x^2 f_X(x) dx for X ~ N(0, sigma^2)
p  = gauss_prob(a, b, sigma);
fa = gauss_pdf(a, sigma);
fb = gauss_pdf(b, sigma);

termA = 0;
termB = 0;

if ~isinf(a)
    termA = a * fa;
end
if ~isinf(b)
    termB = b * fb;
end

m2 = sigma^2 * p + sigma^2 * (termA - termB);
end

function f = gauss_pdf(x, sigma)
if isinf(x)
    f = 0;
else
    f = 1/(sqrt(2*pi)*sigma) * exp(-x.^2/(2*sigma^2));
end
end