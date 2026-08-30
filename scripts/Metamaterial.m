classdef Metamaterial
    % Load and apply the complex transfer response in a miniapp JSON result.
    %
    %   material = Metamaterial("high-pass.json");
    %   filtered = material.apply(samples, sampleRateHz);
    %
    % Frequencies outside the measured range are left unchanged.

    properties (SetAccess = private)
        FrequencyHz
        Transfer
        Metadata
    end

    methods
        function obj = Metamaterial(path)
            data = jsondecode(fileread(path));
            points = data.frequency_response;
            frequency = [];
            transfer = [];

            for index = 1:numel(points)
                point = points(index);
                if point.valid && isscalar(point.transmission) ...
                        && isscalar(point.phase_rad) ...
                        && isfinite(point.transmission) ...
                        && isfinite(point.phase_rad)
                    frequency(end + 1, 1) = point.frequency_hz; %#ok<AGROW>
                    transfer(end + 1, 1) = point.transmission ...
                        * exp(1i * point.phase_rad); %#ok<AGROW>
                end
            end

            if numel(frequency) < 2
                error("Metamaterial:InvalidResult", ...
                    "The result has fewer than two valid FFT bins.");
            end
            [obj.FrequencyHz, order] = sort(frequency);
            obj.Transfer = transfer(order);
            obj.Metadata = data;
        end

        function transfer = response(obj, frequencyHz)
            % Return complex pressure transfer H(f); outside data defaults to 1.
            requestedSize = size(frequencyHz);
            frequencyHz = frequencyHz(:);
            transfer = ones(size(frequencyHz));
            measured = frequencyHz >= obj.FrequencyHz(1) ...
                & frequencyHz <= obj.FrequencyHz(end);
            transfer(measured) = interp1(obj.FrequencyHz, obj.Transfer, ...
                frequencyHz(measured), "linear");
            transfer = reshape(transfer, requestedSize);
        end

        function output = apply(obj, samples, sampleRateHz)
            % Cascade this measured response with one real sampled signal.
            validateattributes(samples, {"numeric"}, {"vector", "real"});
            wasRow = isrow(samples);
            samples = samples(:);
            sampleCount = numel(samples);
            positiveCount = floor(sampleCount / 2) + 1;
            frequency = (0:positiveCount - 1)' * sampleRateHz / sampleCount;
            positiveTransfer = obj.response(frequency);

            if mod(sampleCount, 2) == 0
                fullTransfer = [positiveTransfer; ...
                    conj(positiveTransfer(end - 1:-1:2))];
            else
                fullTransfer = [positiveTransfer; ...
                    conj(positiveTransfer(end:-1:2))];
            end
            output = real(ifft(fft(samples) .* fullTransfer));
            if wasRow
                output = output.';
            end
        end
    end
end
