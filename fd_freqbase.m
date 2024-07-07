function fb = fd_freqbase(n)
    fb = 0:(n - 1);
    fb = fb + floor(n / 2);
    fb = mod(fb, n);
    fb = fb - floor(n / 2);
    
    fb = fb / n; % now [0..0.5[, [-0.5..0[
end
